#include "VideoView.h"
#include "AudioWorker.h"
#include <QPalette>
#include <QVBoxLayout>
#include <QQuickView>
#include <QQuickItem>
#include <QVideoSink>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QThread>
#include <QMetaObject>
#include <QUrl>
#include <QDebug>
#include <algorithm>

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
        connect(root, SIGNAL(contextMenuRequested(qreal,qreal)),
                this, SLOT(onQmlContextMenuRequested(qreal,qreal)));
        connect(root, SIGNAL(wheelScrolled(bool)), this, SLOT(onQmlWheelScrolled(bool)));
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

    // 音声経路は初回 setVolumeBoost() 呼び出し時に確定する（gain で経路を選ぶため）。
    // ここでは player の共通設定のみ行う。

    // 再生速度変更時に音程を保つ（Qt 6.10+ の機能、FFmpeg バックエンド必須）
    qDebug() << "pitchCompensationAvailability:"
             << static_cast<int>(m_player->pitchCompensationAvailability());
    m_player->setPitchCompensation(true);

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

    // audio thread の停止と worker の破棄
    // QAudioSink は audio thread で生成しているため、quit() の前に teardown を
    // BlockingQueuedConnection で呼んで audio thread 上で sink を解放する。
    // GUI thread で sink を破棄すると thread affinity 違反になるため、この順序が必須。
    // m_audioThread と m_audioWorker は独立してガードして、片方が null でも
    // もう一方を確実に解放できるようにする（リーク防止）
    if (m_audioThread) {
        if (m_audioWorker) {
            QMetaObject::invokeMethod(m_audioWorker, "teardown", Qt::BlockingQueuedConnection);
        }
        m_audioThread->quit();
        m_audioThread->wait();
    }
    if (m_audioWorker) {
        delete m_audioWorker;
        m_audioWorker = nullptr;
    }
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
    // ソース切替時にシンクへ積み残ったサンプルを audio thread 側で破棄する。
    // 標準経路（QAudioOutput）使用時は m_audioWorker が無いためスキップする
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "reset", Qt::QueuedConnection);
    }
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
    // 初回呼び出しで音声経路を確定する。
    // gain ≤ 1.0 なら Qt 標準の QAudioOutput 経路（タイミング・クリップ処理を Qt に委譲）。
    // gain > 1.0 なら QAudioBufferOutput + AudioWorker のソフトブースト経路。
    // ランタイムでの経路切替は YAGNI のため未対応（起動時 1 回のみ確定する設計）
    if (!m_audioPathInitialized) {
        m_audioPathInitialized = true;
        if (gain <= 1.0) {
            setupStandardAudioPath(gain);
        }
        else {
            setupBoostAudioPath(gain);
        }
        return;
    }
    // 確定済み経路に対して gain のみ更新する
    if (m_audioOutput) {
        m_audioOutput->setVolume(qBound(0.0, gain, 1.0));
    }
    else if (m_audioWorker) {
        // gain は audio thread の AudioWorker が保持する。書き込みループと同一スレッドで
        // 反映するため QueuedConnection 経由で更新する
        QMetaObject::invokeMethod(m_audioWorker, "setGain", Qt::QueuedConnection,
                                  Q_ARG(double, gain));
    }
}

void VideoView::setupStandardAudioPath(double volume)
{
    // Qt 標準の QAudioOutput を player に接続する。
    // 内部で WASAPI への書き込みタイミングを Qt が管理するため、playbackRate ≠ 1.0 時の
    // atempo フィルタによる burst 出力やオーバーシュートを Qt 側が安全に吸収する。
    // QAudioOutput::setVolume の有効範囲は 0.0〜1.0 のためクランプして渡す
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(qBound(0.0, volume, 1.0));
    m_player->setAudioOutput(m_audioOutput);
}

void VideoView::setupBoostAudioPath(double gain)
{
    // 100% 超のソフトウェア音量ブースト経路。
    // QAudioBufferOutput で Float サンプルを取得し、AudioWorker が gain 乗算＋tanh ソフトクリップを
    // 適用しつつ専用スレッド上の QAudioSink に書き込む。
    // GUI thread が Win32 modal size/move loop でブロックされても音声経路を独立稼働させる狙いがある。
    m_audioBuf = new QAudioBufferOutput(makeAudioFormat(), this);
    m_player->setAudioBufferOutput(m_audioBuf);

    m_audioThread = new QThread(this);
    m_audioWorker = new AudioWorker(makeAudioFormat());
    m_audioWorker->moveToThread(m_audioThread);
    connect(m_audioThread, &QThread::started, m_audioWorker, &AudioWorker::start);

    // decoder thread → audio thread の QueuedConnection 経路。
    // GUI thread を経由しないため、modal loop 中もキュー配送が GUI に滞留しない
    connect(m_audioBuf, &QAudioBufferOutput::audioBufferReceived,
            m_audioWorker, &AudioWorker::onAudioBuffer,
            Qt::QueuedConnection);

    m_audioThread->start();

    // 初期 gain を audio thread に反映する
    QMetaObject::invokeMethod(m_audioWorker, "setGain", Qt::QueuedConnection,
                              Q_ARG(double, gain));
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

void VideoView::onQmlContextMenuRequested(qreal x, qreal y)
{
    // QML の real 座標を受けたまま QPointF で mapToGlobal する。
    // 高 DPI 環境では MouseArea の mouse.x/y が小数を含むため、int で受けると切り捨てで
    // メニュー表示位置がクリック位置と数ピクセルずれる
    const QPoint globalPos = m_quickView->mapToGlobal(QPointF(x, y)).toPoint();
    emit contextMenuRequested(globalPos);
}

void VideoView::onQmlWheelScrolled(bool forward)
{
    emit wheelScrolled(forward);
}
