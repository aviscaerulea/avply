#include "VideoView.h"
#include <cmath>
#include <QPalette>
#include <QVBoxLayout>
#include <QQuickView>
#include <QQuickItem>
#include <QVideoSink>
#include <QMediaPlayer>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QByteArray>
#include <QEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDebug>

namespace {

// QAudioBufferOutput / QAudioSink で共通使用する固定オーディオフォーマット
// 48000Hz / ステレオ / Float サンプル
// Float に固定することで gain 乗算とクリップ処理が単純化される
QAudioFormat makeAudioFormat()
{
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);
    return fmt;
}

} // namespace

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
    , m_quickView(new QQuickView)
    , m_player(new QMediaPlayer(this))
    , m_audioBuf(new QAudioBufferOutput(makeAudioFormat(), this))
    , m_sink(new QAudioSink(makeAudioFormat(), this))
{
    // VideoView 本体の背景を即時に黒系に設定する。
    // QQuickView のネイティブサーフェス確立前にコンテナが白く見えるのを防ぐ
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x1a, 0x1a, 0x1a));
    setPalette(pal);
    setAutoFillBackground(true);

    // QML をロードして VideoOutput のシンクを QMediaPlayer に接続する。
    // threaded render loop が有効な場合、render thread が Win32 modal loop 中も
    // IDXGISwapChain::Present を独立して呼べるため、ドラッグ中も再生が継続する
    m_quickView->setColor(QColor(0x1a, 0x1a, 0x1a));
    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);

    // QML の Ready 時に VideoOutput の videoSink を QMediaPlayer に接続する。
    // setSource() は同期的に statusChanged を emit するため、必ず setSource() より前に connect する
    connect(m_quickView, &QQuickView::statusChanged,
            this, [this](QQuickView::Status status) {
        if (status != QQuickView::Ready) return;
        QObject* root = m_quickView->rootObject();
        if (!root) return;
        auto* sink = root->property("videoSink").value<QVideoSink*>();
        if (sink) {
            m_player->setVideoSink(sink);
        }
        // QML シグナルを C++ スロットに接続する（SIGNAL/SLOT マクロ形式が QML シグナルに対応）
        connect(root, SIGNAL(clicked()), this, SLOT(onQmlClicked()));
        connect(root, SIGNAL(wheelScrolled(bool)), this, SLOT(onQmlWheelScrolled(bool)));
        connect(root, SIGNAL(fileDropped(QString)), this, SLOT(onQmlFileDropped(QString)));
    });

    m_quickView->setSource(QUrl("qrc:/VideoOutput.qml"));

    // QQuickView を QWidget として埋め込む。createWindowContainer は D&D 非対応のため
    // ドロップは VideoView 自体（このウィジェット）の dragEnterEvent/dropEvent で受け取る
    m_videoContainer = QWidget::createWindowContainer(m_quickView, this);
    m_videoContainer->setMinimumSize(1, 1);
    m_videoContainer->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoContainer);

    // 100% 超のソフトウェア音量ブーストのため QAudioOutput は接続せず、
    // QAudioBufferOutput でサンプルを取得して QAudioSink に push する
    m_player->setAudioBufferOutput(m_audioBuf);

    // ウィンドウドラッグ中の音声途切れ対策で QAudioSink バッファを 500ms に拡張する。
    // modal size/move loop 中は WM_TIMER 経由（≈70ms 間隔）でしか audioBufferReceived を
    // drain できないため、WASAPI 既定（10〜30ms）のままだと空白期間にアンダーランして
    // 無音化する。500ms 確保しておけば WM_TIMER 数周期分の遅延も吸収できる
    constexpr int kAudioBufferBytes = 48000 * 2 * sizeof(float) * 500 / 1000;
    m_sink->setBufferSize(kAudioBufferBytes);
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "QAudioSink::start() failed:" << m_sink->error();
    }

    // 再生速度変更時に音程を保つ（Qt 6.10+ の機能、FFmpeg バックエンド必須）
    qDebug() << "pitchCompensationAvailability:"
             << static_cast<int>(m_player->pitchCompensationAvailability());
    m_player->setPitchCompensation(true);

    // 音声バッファ受信時に gain を適用して QAudioSink へ書き込む
    // Float サンプルを線形乗算し tanhf で ±1.0 付近に滑らかに飽和させる（ソフトクリップ）。
    // ハードクリップ（直角カット）は高調波歪み（ザリザリ音）を生むため避ける。
    // 特に playbackRate 変更時の resampler は overshoot サンプル（±1.0 超）を生成するため
    // gain 1.0 でもクリップ経路を通る場合に問題化する
    connect(m_audioBuf, &QAudioBufferOutput::audioBufferReceived,
            this, [this](const QAudioBuffer& buf) {
        if (!m_sinkDev) return;
        if (buf.format().sampleFormat() != QAudioFormat::Float) return;

        const int n = buf.byteCount() / static_cast<int>(sizeof(float));
        const float* src = buf.constData<float>();
        QByteArray out(buf.byteCount(), Qt::Uninitialized);
        float* dst = reinterpret_cast<float*>(out.data());
        const float g = static_cast<float>(m_gain);
        for (int i = 0; i < n; ++i) {
            dst[i] = std::tanh(src[i] * g);
        }
        m_sinkDev->write(out);
    });

    // D&D は createWindowContainer では機能しないため VideoView 自体で受け付ける
    setAcceptDrops(true);

    // 末尾到達時に Qt が自動で StoppedState（位置 0 に戻る）へ遷移するのを抑止する。
    // 終端の数十 ms 手前で先取りで pause を呼び、ユーザがそこからシークバーで微調整
    // できるようにする。pause() は非同期完了のため、再入防止フラグで一度だけ発火させる
    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) {
        // 新ソース読み込み中は旧ソースの遅延 positionChanged を破棄する
        if (m_primeFirstFrame) return;
        const qint64 dur = m_player->duration();
        if (!m_pausingAtEnd && dur > 0 && pos >= dur - 50 && isPlaying()) {
            m_pausingAtEnd = true;
            m_player->pause();
        }
        emit positionChanged(pos);
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        // 再生中以外への遷移時に末尾自動 pause フラグを解除する
        if (state != QMediaPlayer::PausedState) m_pausingAtEnd = false;
        emit playbackStateChanged(state == QMediaPlayer::PlayingState);
    });

    // メディア読み込み完了時に再生を開始する
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus s) {
        if (!m_primeFirstFrame) return;
        if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) {
            m_primeFirstFrame = false;
            m_videoContainer->show();
            m_player->play();
        }
    });
}

VideoView::~VideoView() = default;

void VideoView::setSource(const QString& filePath)
{
    m_player->stop();
    m_primeFirstFrame = true;
    m_pausingAtEnd = false;
    m_player->setSource(QUrl::fromLocalFile(filePath));
}

void VideoView::clear()
{
    m_primeFirstFrame = false;
    m_pausingAtEnd = false;
    m_player->stop();
    m_player->setSource(QUrl());
    m_videoContainer->hide();
    // ソース切替時にシンクへ積み残ったサンプルを破棄する
    m_sink->reset();
    m_sinkDev = m_sink->start();
}

qint64 VideoView::position() const
{
    return m_player->position();
}

void VideoView::setPosition(qint64 ms)
{
    // 手動シークで末尾自動 pause フラグを解除する
    m_pausingAtEnd = false;
    m_player->setPosition(ms);
}

void VideoView::setPlaybackRate(qreal rate)
{
    m_player->setPlaybackRate(rate);
}

void VideoView::setVolumeBoost(double gain)
{
    m_gain = gain;
}

void VideoView::togglePlay()
{
    if (isPlaying()) {
        m_player->pause();
    }
    else {
        m_player->play();
    }
}

void VideoView::pause()
{
    if (isPlaying()) m_player->pause();
}

void VideoView::setInteractive(bool enabled)
{
    m_interactive = enabled;
}

bool VideoView::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

QSize VideoView::sizeHint() const
{
    return QSize(800, 450);
}

QSize VideoView::minimumSizeHint() const
{
    return QSize(320, 180);
}

void VideoView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0) emit wheelScrolled(delta > 0);
    event->accept();
}

void VideoView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VideoView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void VideoView::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            emit fileDropped(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}

void VideoView::onQmlClicked()
{
    if (m_interactive && !m_player->source().isEmpty()) {
        togglePlay();
    }
}

void VideoView::onQmlWheelScrolled(bool forward)
{
    emit wheelScrolled(forward);
}

void VideoView::onQmlFileDropped(const QString& url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile()) {
        emit fileDropped(parsed.toLocalFile());
    }
}
