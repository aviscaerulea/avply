#include "VideoView.h"
#include "AudioWorker.h"
#include "Config.h"
#include "Settings.h"
#include <QPalette>
#include <QVBoxLayout>
#include <QQuickView>
#include <QQuickItem>
#include <QVideoSink>
#include <QMediaPlayer>
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QThread>
#include <QMetaObject>
#include <QEvent>
#include <QWheelEvent>
#include <QUrl>
#include <QDebug>
#include <algorithm>

namespace {

// QAudioBufferOutput / QAudioSink で共通使用する固定オーディオフォーマット
// 48000Hz / ステレオ / Float に固定することでサンプル処理（DSP）が単純化される
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
        // QML シグナルを C++ スロットに接続する。
        // QML 側のシグネチャと不一致になっていれば connect が失敗するため警告ログで通知する
        const std::pair<const char*, const char*> qmlConns[] = {
            { SIGNAL(clicked()),
              SLOT(onQmlClicked()) },
            { SIGNAL(contextMenuRequested(qreal,qreal)),
              SLOT(onQmlContextMenuRequested(qreal,qreal)) },
            { SIGNAL(wheelScrolled(bool,bool,bool)),
              SLOT(onQmlWheelScrolled(bool,bool,bool)) },
            { SIGNAL(fileDropped(QString)),
              SLOT(onQmlFileDropped(QString)) },
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

    // 音声経路を QAudioBufferOutput + 専用スレッド AudioWorker + QAudioSink で構成する。
    // decoder thread → audio thread の QueuedConnection 経路により、
    // GUI thread が modal loop でブロックされても音声経路が独立稼働する。
    // 初期 DSP 状態は Settings（強度レベル）から、強度別パラメータは avply.toml から読み込む。
    // Config::load() は MainWindow でも別途呼ばれるが、ステートレスかつ起動時 1 回のため重複コストは無視できる
    const QAudioFormat audioFmt = makeAudioFormat();
    const AppConfig    cfg      = Config::load();
    const Normalizer::LevelParams normSmall {
        static_cast<float>(cfg.normalizerThresholdDbSmall),
        static_cast<float>(cfg.normalizerMakeupDbSmall),
    };
    const Normalizer::LevelParams normMedium {
        static_cast<float>(cfg.normalizerThresholdDbMedium),
        static_cast<float>(cfg.normalizerMakeupDbMedium),
    };
    const Normalizer::LevelParams normLarge {
        static_cast<float>(cfg.normalizerThresholdDbLarge),
        static_cast<float>(cfg.normalizerMakeupDbLarge),
    };
    const VoiceClarity::LevelParams vcSmall {
        static_cast<float>(cfg.voiceClarityPeakDbSmall),
        static_cast<float>(cfg.voiceClarityShelfDbSmall),
    };
    const VoiceClarity::LevelParams vcMedium {
        static_cast<float>(cfg.voiceClarityPeakDbMedium),
        static_cast<float>(cfg.voiceClarityShelfDbMedium),
    };
    const VoiceClarity::LevelParams vcLarge {
        static_cast<float>(cfg.voiceClarityPeakDbLarge),
        static_cast<float>(cfg.voiceClarityShelfDbLarge),
    };
    m_audioBuf    = new QAudioBufferOutput(audioFmt, this);
    m_audioThread = new QThread(this);
    m_audioWorker = new AudioWorker(audioFmt,
                                    Settings::instance().normalizeLevel(),
                                    Settings::instance().voiceClarityLevel(),
                                    normSmall, normMedium, normLarge,
                                    vcSmall, vcMedium, vcLarge);
    m_audioWorker->moveToThread(m_audioThread);

    connect(m_audioThread, &QThread::started, m_audioWorker, &AudioWorker::start);
    connect(m_audioBuf, &QAudioBufferOutput::audioBufferReceived,
            m_audioWorker, &AudioWorker::onAudioBuffer,
            Qt::QueuedConnection);

    m_player->setAudioBufferOutput(m_audioBuf);
    // audio thread を HighPriority で起動する
    // 高速再生時の DSP 負荷で QAudioSink::write が間に合わず underrun が出ていたため、
    // GUI/decoder thread に対し audio sink 書き込みを優先させる。
    // TimeCriticalPriority は OS スケジューラ独占リスクがあるため避ける
    m_audioThread->start(QThread::HighPriority);

    // pitchCompensation の呼び出し（Qt 将来バージョン互換のための予約）
    // Qt 6.10 の QAudioBufferOutput パスでは setPitchCompensation(true) は無視され、
    // 実際のピッチ補正は AudioWorker 内の SoundTouch が担う。
    // 将来 QAudioBufferOutput パスで Qt がピッチ補正を実装した場合の自動有効化を兼ねて呼ぶ。
    // availability ログは「Qt 側で実装が入った時点を検知する診断」用に残す
    qDebug() << "pitchCompensationAvailability:"
             << static_cast<int>(m_player->pitchCompensationAvailability());
    m_player->setPitchCompensation(true);

    // 再生位置の伝搬。
    // 末尾到達時の pause は mediaStatusChanged の EndOfMedia ハンドラ側で行う。
    // positionChanged ベースで先取り pause する旧実装は、ffmpeg backend で mp3 等の
    // 発火粒度が数百 ms 〜 1 秒程度になるケースで「閾値到達したが末尾は踏み越えていない」
    // 区間が未再生のまま残る不具合があった
    connect(m_player, &QMediaPlayer::positionChanged,
            this, [this](qint64 pos) {
        // 新ソース読み込み中は旧ソースの遅延 positionChanged を破棄する
        if (m_primeFirstFrame) return;
        emit positionChanged(pos);
    });

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        // PausedState 以外への遷移時に末尾自動 pause フラグを解除する
        // Playing / Stopped どちらでも解除する（末尾 pause 状態の維持は Paused のみ）
        if (state != QMediaPlayer::PausedState) m_pausingAtEnd = false;
        emit playbackStateChanged(state == QMediaPlayer::PlayingState);
    });

    // メディア状態遷移ハンドラ。
    // 初回ロード完了時の自動再生と、末尾到達時の pause を扱う
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus s) {
        // 初回ロード完了時に再生を開始する
        if (m_primeFirstFrame &&
            (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia)) {
            m_primeFirstFrame = false;
            // hasVideo が true のときのみ QQuickView コンテナを表示する。
            // 音声のみのソースで表示すると VideoOutput に何も描画されず黒矩形が残るため、
            // 描画対象が確定したフレームでだけコンテナを可視化する
            if (m_player->hasVideo()) {
                m_videoContainer->show();
            }
            m_player->play();
            return;
        }
        // 末尾到達時の pause（Qt が自動で StoppedState へ遷移して位置 0 にリセット
        // するのを抑止する）。EndOfMedia は確実な末尾検出のため、ffmpeg backend の
        // positionChanged 発火粒度に依存せず未再生区間を残さない。
        // pause 直後に position を duration へ固定し、シークバー表示を末尾に張り付ける
        // （backend によっては EndOfMedia 発火時点の内部 position が dur 手前で
        // 止まっており、そのまま pause すると最後の数百 ms 分シークバーが届かない）。
        // 内部 m_player->setPosition は VideoView::setPosition を経由せず m_pausingAtEnd を
        // 維持するため、再 EndOfMedia 発火でも本ブランチは再入しない
        if (s == QMediaPlayer::EndOfMedia && !m_pausingAtEnd) {
            m_pausingAtEnd = true;
            m_player->pause();
            const qint64 dur = m_player->duration();
            if (dur > 0) {
                m_player->setPosition(dur);
            }
        }
    });
}

VideoView::~VideoView()
{
    // QMediaPlayer が破棄前に videoSink へフレームを書き込まないよう先に切断する。
    // QQuickView 側の VideoOutput より QMediaPlayer のほうが寿命が長いケースに備え、
    // 明示的に nullptr を設定してフレーム転送経路を断つ
    if (m_player) m_player->setVideoSink(nullptr);

    // audio thread の停止と worker の破棄。
    // QAudioSink は audio thread で生成しているため、quit() より前に teardown を
    // BlockingQueuedConnection で呼んで audio thread 上で sink を解放する。
    // GUI thread で sink を破棄すると thread affinity 違反になるため、この順序が必須
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
    // ソース切替時に sink の積み残しサンプルと Normalizer 状態をリセットする
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "reset", Qt::QueuedConnection);
    }
    m_player->setSource(QUrl::fromLocalFile(filePath));
}

void VideoView::clear()
{
    m_primeFirstFrame = false;
    m_pausingAtEnd = false;
    m_player->stop();
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "reset", Qt::QueuedConnection);
    }
    m_player->setSource(QUrl());
    m_videoContainer->hide();
}

qint64 VideoView::position() const
{
    return m_player->position();
}

void VideoView::setPosition(qint64 ms)
{
    // 手動シークで末尾自動 pause フラグを解除し、sink の積み残しと Normalizer 状態をリセットする
    m_pausingAtEnd = false;
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "reset", Qt::QueuedConnection);
    }
    m_player->setPosition(ms);
}

void VideoView::setPlaybackRate(qreal rate)
{
    // AudioWorker 側の SoundTouch tempo を先に更新する。
    // QAudioBufferOutput が pitchCompensation を無視するため、AudioWorker 内 SoundTouch で
    // 同じ rate ぶんの時間圧縮 / 伸長を行わないと sink 流入と消費がミスマッチを起こす。
    // QMediaPlayer の rate 変更で decoder の流入レートが変わる前に tempo を合わせることで、
    // 旧 tempo 前提の入力が SoundTouch 内に滞留して sink underrun を引き起こすのを防ぐ。
    // DirectConnection で呼び出すが、setPlaybackRate 側は atomic 経由で受け取るため
    // GUI thread から直接呼んでもスレッド安全
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "setPlaybackRate", Qt::DirectConnection,
                                  Q_ARG(double, static_cast<double>(rate)));
    }
    m_player->setPlaybackRate(rate);
}

void VideoView::setVolume(double volume)
{
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "setVolume", Qt::QueuedConnection,
                                  Q_ARG(double, qBound(0.0, volume, 1.0)));
    }
}

void VideoView::setNormalizeLevel(int level)
{
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "setNormalizeLevel", Qt::QueuedConnection,
                                  Q_ARG(int, level));
    }
}

void VideoView::setVoiceClarityLevel(int level)
{
    if (m_audioWorker) {
        QMetaObject::invokeMethod(m_audioWorker, "setVoiceClarityLevel", Qt::QueuedConnection,
                                  Q_ARG(int, level));
    }
}

void VideoView::togglePlay()
{
    if (isPlaying()) {
        m_player->pause();
    }
    else {
        // 末尾到達 pause 状態からの再生要求は先頭から再生する。
        // setPosition() 経由で AudioWorker::reset を発出し、前回再生末尾の
        // partial-write 残量（SoundTouch 内部バッファと m_pendingTail）が
        // 先頭区間に貼り付くプチノイズを防ぐ
        if (m_pausingAtEnd) {
            setPosition(0);
        }
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
    const auto mods = event->modifiers();
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool ctrl  = mods.testFlag(Qt::ControlModifier);
    if (delta != 0) emit wheelScrolled(delta > 0, shift, ctrl);
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

void VideoView::onQmlWheelScrolled(bool forward, bool shift, bool ctrl)
{
    emit wheelScrolled(forward, shift, ctrl);
}

void VideoView::onQmlFileDropped(const QString& url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile()) {
        emit fileDropped(parsed.toLocalFile());
    }
}
