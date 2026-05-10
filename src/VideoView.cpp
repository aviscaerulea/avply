#include "VideoView.h"
#include <QPalette>
#include <QVBoxLayout>
#include <QQuickView>
#include <QQuickItem>
#include <QVideoSink>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QEvent>
#include <QWheelEvent>
#include <QUrl>
#include <QDebug>
#include <algorithm>

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
    , m_quickView(new QQuickView)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
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
        // QML シグナルを C++ スロットに接続する。
        // SIGNAL/SLOT マクロは文字列照合のためビルド時の型チェックが効かない。
        // QObject::connect の戻り値（QMetaObject::Connection）を検査し、
        // QML 側のシグネチャと不一致になっていれば起動時に警告ログを出す
        const std::pair<const char*, const char*> qmlConns[] = {
            { "2clicked()",                        "1onQmlClicked()" },
            { "2contextMenuRequested(qreal,qreal)","1onQmlContextMenuRequested(qreal,qreal)" },
            { "2wheelScrolled(bool,bool)",         "1onQmlWheelScrolled(bool,bool)" },
            { "2fileDropped(QString)",             "1onQmlFileDropped(QString)" },
        };
        for (const auto& [sig, slot] : qmlConns) {
            if (!connect(root, sig, this, slot)) {
                qWarning() << "VideoView: QML signal connect failed:" << sig;
            }
        }
    });

    m_quickView->setSource(QUrl("qrc:/VideoOutput.qml"));

    // QQuickView を QWidget として埋め込む。
    // D&D は QML 側 DropArea で受ける（createWindowContainer の埋め込み HWND が
    // 親 QWidget の dragEnter/drop へイベントを伝搬しないため）
    m_videoContainer = QWidget::createWindowContainer(m_quickView, this);
    m_videoContainer->setMinimumSize(1, 1);
    m_videoContainer->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoContainer);

    m_player->setAudioOutput(m_audioOutput);

    // 再生速度変更時に音程を保つ（Qt 6.10+ の機能、FFmpeg バックエンド必須）
    qDebug() << "pitchCompensationAvailability:"
             << static_cast<int>(m_player->pitchCompensationAvailability());
    m_player->setPitchCompensation(true);

    // 末尾到達時に Qt が自動で StoppedState（位置 0 に戻る）へ遷移するのを抑止する。
    // 終端の数十 ms 手前で先取りで pause を呼び、ユーザがそこからシークバーで微調整
    // できるようにする。pause() は非同期完了のため、再入防止フラグで一度だけ発火させる
    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) {
        // 新ソース読み込み中は旧ソースの遅延 positionChanged を破棄する
        if (m_primeFirstFrame) return;
        const qint64 dur = m_player->duration();
        // 末尾自動 pause の閾値を再生速度に応じて動的に伸ばす。
        // playbackRate が大きいと positionChanged の発火間隔が wall-clock 上縮まる代わりに
        // メディア時間軸上は粗くなり、固定 50ms では末尾を踏み越える。
        // max(50, 100*rate) で 1.0x は 100ms、4.0x なら 400ms 手前で確実に pause する。
        // rate を qMax で 1.0 にクランプすることで、想定外の負値・0 でも下限 50ms を維持する
        const qreal rate = std::max<qreal>(1.0, m_player->playbackRate());
        const qint64 threshold = static_cast<qint64>(100.0 * rate);
        if (!m_pausingAtEnd && dur > 0 && pos >= dur - threshold && isPlaying()) {
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
            // hasVideo が true のときのみ QQuickView コンテナを表示する。
            // 音声のみのソースで表示すると VideoOutput に何も描画されず黒矩形が残るため、
            // 描画対象が確定したフレームでだけコンテナを可視化する
            if (m_player->hasVideo()) {
                m_videoContainer->show();
            }
            m_player->play();
        }
    });
}

VideoView::~VideoView()
{
    // QMediaPlayer が破棄前に videoSink へフレームを書き込まないよう先に切断する。
    // QQuickView 側の VideoOutput より QMediaPlayer のほうが寿命が長いケースに備え、
    // 明示的に nullptr を設定してフレーム転送経路を断つ
    if (m_player) m_player->setVideoSink(nullptr);
}

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

void VideoView::setVolume(double volume)
{
    m_audioOutput->setVolume(qBound(0.0, volume, 1.0));
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
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if (delta != 0) emit wheelScrolled(delta > 0, shift);
    event->accept();
}

void VideoView::onQmlClicked()
{
    if (m_interactive && !m_player->source().isEmpty()) {
        togglePlay();
    }
}

void VideoView::onQmlContextMenuRequested(qreal x, qreal y)
{
    // QML の real 座標を受けたまま QPointF で mapToGlobal する。
    // 高 DPI 環境では MouseArea の mouse.x/y が小数を含むため、int で受けると切り捨てで
    // メニュー表示位置がクリック位置と数ピクセルずれる
    const QPoint globalPos = m_quickView->mapToGlobal(QPointF(x, y)).toPoint();
    emit contextMenuRequested(globalPos);
}

void VideoView::onQmlWheelScrolled(bool forward, bool shift)
{
    emit wheelScrolled(forward, shift);
}

void VideoView::onQmlFileDropped(const QString& url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile()) {
        emit fileDropped(parsed.toLocalFile());
    }
}
