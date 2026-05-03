#include "VideoView.h"
#include <QPalette>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QEvent>
#include <QMouseEvent>
#include <QChildEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDebug>

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
    , m_videoWidget(new QVideoWidget(this))
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
{
    // VideoView 本体の背景を即時に黒系に設定する。
    // QVideoWidget のネイティブサーフェス確立前にコンテナが白く見えるのを防ぐ
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x1a, 0x1a, 0x1a));
    setPalette(pal);
    setAutoFillBackground(true);

    m_videoWidget->setStyleSheet("background: #1a1a1a;");
    // 動画未ロード時は QVideoWidget を隠して VideoView 本体の暗色背景のみ見せる。
    // ネイティブサーフェス確立前の白フラッシュを起動時に発生させないための処置
    m_videoWidget->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoWidget);

    m_player->setVideoOutput(m_videoWidget);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(0.8);

    // 再生速度変更時に音程を保つ（Qt 6.10+ の機能、FFmpeg バックエンド必須）
    // 利用可否を qDebug に出して実機検証時の判断材料とする
    qDebug() << "pitchCompensationAvailability:"
             << static_cast<int>(m_player->pitchCompensationAvailability());
    m_player->setPitchCompensation(true);

    // VideoView 本体および QVideoWidget の両方で D&D を受け付ける
    // QVideoWidget はレンダリング用のネイティブ子ウィジェットを内部に作るため、
    // 後から増える子にも acceptDrops と eventFilter を再帰的に張る必要がある
    setAcceptDrops(true);
    m_videoWidget->setAcceptDrops(true);
    m_videoWidget->installEventFilter(this);
    for (QWidget* w : m_videoWidget->findChildren<QWidget*>()) {
        w->setAcceptDrops(true);
        w->installEventFilter(this);
    }

    connect(m_player, &QMediaPlayer::positionChanged,
            this, &VideoView::positionChanged);

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        emit playbackStateChanged(state == QMediaPlayer::PlayingState);
    });

    // メディア読み込み完了時に再生を開始する
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus s) {
        if (!m_primeFirstFrame) return;
        if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) {
            m_primeFirstFrame = false;
            // ネイティブサーフェス確立とフレーム描画タイミングを近づけるため
            // show() はメディアロード完了直前まで遅延させる
            m_videoWidget->show();
            m_player->play();
        }
    });
}

VideoView::~VideoView() = default;

void VideoView::setSource(const QString& filePath)
{
    m_player->stop();
    m_primeFirstFrame = true;
    m_player->setSource(QUrl::fromLocalFile(filePath));
}

void VideoView::clear()
{
    m_primeFirstFrame = false;
    m_player->stop();
    m_player->setSource(QUrl());
    m_videoWidget->hide();
}

qint64 VideoView::position() const
{
    return m_player->position();
}

void VideoView::setPosition(qint64 ms)
{
    m_player->setPosition(ms);
}

void VideoView::setPlaybackRate(qreal rate)
{
    m_player->setPlaybackRate(rate);
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

bool VideoView::eventFilter(QObject* watched, QEvent* event)
{
    // 監視対象は m_videoWidget またはその子孫ウィジェットのみ
    bool isVideoTree = false;
    for (QObject* o = watched; o; o = o->parent()) {
        if (o == m_videoWidget) { isVideoTree = true; break; }
    }
    if (!isVideoTree) return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::ChildAdded: {
        // QVideoWidget が遅延生成する子ウィジェットにもフィルタと acceptDrops を伝播する
        auto* ce = static_cast<QChildEvent*>(event);
        if (auto* w = qobject_cast<QWidget*>(ce->child())) {
            w->setAcceptDrops(true);
            w->installEventFilter(this);
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && !m_player->source().isEmpty()) {
            togglePlay();
            return true;
        }
        break;
    }
    case QEvent::DragEnter: {
        if (!acceptDrops()) break;
        auto* de = static_cast<QDragEnterEvent*>(event);
        if (de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        if (!acceptDrops()) break;
        auto* de = static_cast<QDragMoveEvent*>(event);
        if (de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        if (!acceptDrops()) break;
        auto* de = static_cast<QDropEvent*>(event);
        for (const QUrl& url : de->mimeData()->urls()) {
            if (url.isLocalFile()) {
                emit fileDropped(url.toLocalFile());
                de->acceptProposedAction();
                return true;
            }
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}
