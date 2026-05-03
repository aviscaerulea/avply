#include "MainWindow.h"
#include "Config.h"
#include "OutputNamer.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QResizeEvent>
#include <QMimeData>
#include <QUrl>
#include <QSignalBlocker>
#include <QTimer>
#include <QStatusBar>
#include <QKeyEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>
#include <QIcon>
#include <algorithm>
#include <cmath>

static constexpr int kSliderMax = 10000;

MainWindow::MainWindow(const QString& initialPath, QWidget* parent)
    : QMainWindow(parent)
    , m_encoder(nullptr)
{
    setWindowTitle("avply");
    setAcceptDrops(true);

    // --- ファイル選択行 ---
    m_filePathLabel = new QLabel("動画ファイルを選択するか、ウィンドウへドロップしてください");
    m_filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_openBtn = new QPushButton("開く...");
    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::onOpenFile);

    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(m_filePathLabel, 1);
    fileRow->addWidget(m_openBtn);

    // --- 動画プレビュー（クリックで再生/停止トグル、D&D でファイル読み込み） ---
    m_videoView = new VideoView;
    connect(m_videoView, &VideoView::positionChanged,
            this, &MainWindow::onPlayerPositionChanged);
    connect(m_videoView, &VideoView::fileDropped,
            this, [this](const QString& path) {
        if (isAcceptedVideo(path)) loadFile(path);
    });

    // --- 再生位置ラベル（ステータスバー右端に配置） ---
    // 先頭 2 半角スペースは項目間の区切りとして機能する
    m_posLabel = new QLabel("  --:--:-- / --:--:--");

    // --- 再生速度ラベル（ステータスバー右端、再生位置の右に配置） ---
    m_speedLabel = new QLabel("  x1.00");

    // --- シークスライダー ---
    m_seekSlider = new RangeSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, kSliderMax);
    m_seekSlider->setEnabled(false);
    // valueChanged を使うことでクリックジャンプの位置も拾える
    connect(m_seekSlider, &QSlider::valueChanged,
            this, &MainWindow::onSeekSliderChanged);

    // アイコン式ボタン共通スタイル
    // 外枠と内側パディングを消し、ホバー時のみ薄いグレーで反応を示す
    // padding: 0 を入れないとテキストボタン（【】）でホバー範囲が縦に膨らみ、
    // アイコンボタン（再生・停止）と見た目のサイズが揃わない
    const QString iconBtnStyle =
        "QPushButton { border: none; padding: 0; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 30); }";
    // アイコン式ボタンの統一サイズ（再生・停止・【・】 すべて同じ矩形でホバーする）
    const QSize iconBtnSize(36, 32);

    // --- 再生/一時停止ボタン（シークバー左、再生状態の視認も兼ねる） ---
    // PNG アイコンを使用する
    m_iconPlay  = QIcon(":/icons/play.png");
    m_iconPause = QIcon(":/icons/pause.png");
    m_playPauseBtn = new QPushButton;
    m_playPauseBtn->setIcon(m_iconPlay);
    m_playPauseBtn->setIconSize(QSize(24, 24));
    connect(m_playPauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_info.valid) m_videoView->togglePlay();
    });
    connect(m_videoView, &VideoView::playbackStateChanged,
            this, [this](bool playing) {
        m_playPauseBtn->setIcon(playing ? m_iconPause : m_iconPlay);
    });

    // --- 停止ボタン（シーク位置を 0 に戻し、開始/終了マーカーをクリアする） ---
    m_stopBtn = new QPushButton;
    m_stopBtn->setIcon(QIcon(":/icons/stop.png"));
    m_stopBtn->setIconSize(QSize(24, 24));
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    // --- 開始/終了 設定ボタン（再生/停止と同じアイコン式スタイルに揃える） ---
    m_setInBtn  = new QPushButton("【");
    m_setOutBtn = new QPushButton("】");
    connect(m_setInBtn,  &QPushButton::clicked, this, &MainWindow::onSetIn);
    connect(m_setOutBtn, &QPushButton::clicked, this, &MainWindow::onSetOut);

    // 4 つのアイコン式ボタンに共通スタイルとサイズを一括適用する
    for (QPushButton* b : { m_playPauseBtn, m_stopBtn, m_setInBtn, m_setOutBtn }) {
        b->setStyleSheet(iconBtnStyle);
        b->setFixedSize(iconBtnSize);
        b->setEnabled(false);
    }

    // --- 変換ボタン（シークバー行の右側に配置する） ---
    m_convertBtn = new QPushButton("変換");
    m_convertBtn->setFixedWidth(120);
    m_convertBtn->setEnabled(false);
    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::onConvertOrCancel);

    // 左側アイコン群を内側レイアウトでまとめ、ボタン同士をピッタリ隣接させる
    auto* leftIconRow = new QHBoxLayout;
    leftIconRow->setSpacing(0);
    leftIconRow->setContentsMargins(0, 0, 0, 0);
    leftIconRow->addWidget(m_playPauseBtn);
    leftIconRow->addWidget(m_stopBtn);
    leftIconRow->addWidget(m_setInBtn);

    auto* seekRow = new QHBoxLayout;
    seekRow->setSpacing(4);
    seekRow->addLayout(leftIconRow);
    seekRow->addWidget(m_seekSlider, 1);
    seekRow->addWidget(m_setOutBtn);
    seekRow->addWidget(m_convertBtn);

    // --- 動画情報ラベル（ステータスバー左端、解像度・動画形式・音声形式） ---
    m_videoInfoLabel = new QLabel;
    m_videoInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // --- 出力ファイルラベル（ステータスバー、動画情報の右） ---
    m_outputLabel = new QLabel;
    m_outputLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // --- メインレイアウト ---
    auto* central = new QWidget;
    auto* main    = new QVBoxLayout(central);
    main->setSpacing(8);
    // bottom はわずかな余白だけ残して開始/終了行とステータスバーの間隔を詰める
    main->setContentsMargins(12, 12, 12, 4);
    main->addLayout(fileRow);
    // 余剰スペースを全てプレビューに割り当ててウィンドウリサイズに追従させる
    main->addWidget(m_videoView, 1);
    main->addLayout(seekRow);

    setCentralWidget(central);

    // --- ステータスバー：左から動画情報・出力状況、右に再生位置と再生速度 ---
    // 項目間の縦罫線を非表示にして、ラベル先頭の半角スペースのみで間隔を作る
    statusBar()->setStyleSheet("QStatusBar::item { border: none; }");
    statusBar()->addWidget(m_videoInfoLabel);
    statusBar()->addWidget(m_outputLabel, 1);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_speedLabel);

    // シーク要求スロットル：先頭は即時、後続は 40ms 間隔で最新値を反映
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(40);
    connect(&m_seekTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingSeekMs < 0) return;
        m_videoView->setPosition(m_pendingSeekMs);
        m_pendingSeekMs = -1;
        m_seekTimer.start();
    });

    // 設定読込
    const AppConfig cfg = Config::load();
    m_ffmpegPath           = cfg.ffmpegPath;
    m_seekLeftMs           = cfg.seekLeftMs;
    m_seekRightMs          = cfg.seekRightMs;
    m_initialScreenRatio   = cfg.initialScreenRatio;
    m_playbackRate = cfg.playbackRate;
    updateSpeedDisplay();
    // アプリケーション全体のキー入力を捕捉して左右カーソルシークに変換する
    qApp->installEventFilter(this);
    // ウィンドウ表示後に検証する（show 前のダイアログ表示を避ける）
    QTimer::singleShot(0, this, &MainWindow::validateFfmpegPath);
    // レイアウト確定後に下部UI高を保存し、最小ウィンドウサイズを連動式で設定する
    QTimer::singleShot(0, this, [this]() {
        adjustSize();
        m_lowerUiH = height() - m_videoView->height();
        updateMinimumWindowSize();
    });

    // コマンドラインで初期ファイルが指定されていれば起動完了後に読み込む
    // 拡張子フィルタを通すことで非対応形式は静かに無視する
    if (!initialPath.isEmpty()) {
        QTimer::singleShot(0, this, [this, initialPath]() {
            if (isAcceptedVideo(initialPath) && QFile::exists(initialPath)) {
                loadFile(initialPath);
            }
        });
    }
}

MainWindow::~MainWindow() = default;

// ---- ドラッグ＆ドロップ ----

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isAcceptedVideo(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (isAcceptedVideo(path)) {
            loadFile(path);
            event->acceptProposedAction();
            return;
        }
    }
}

// ---- スロット実装 ----

void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "動画ファイルを開く", openDialogStartDir(),
        "動画ファイル (*.mp4 *.mkv *.mov *.avi *.webm);;すべてのファイル (*)");
    if (path.isEmpty()) return;
    loadFile(path);
}

void MainWindow::onSeekSliderChanged(int value)
{
    if (m_info.duration <= 0.0) return;
    const qint64 ms = static_cast<qint64>(sliderToSec(value) * 1000.0);
    // 先頭の要求は即時反映し、後続はタイマーで 40ms ごとに最新値だけ反映する
    m_pendingSeekMs = ms;
    if (!m_seekTimer.isActive()) {
        m_videoView->setPosition(ms);
        m_pendingSeekMs = -1;
        m_seekTimer.start();
    }
}

void MainWindow::onPlayerPositionChanged(qint64 ms)
{
    const double sec = ms / 1000.0;
    m_posLabel->setText("  " + formatSec(sec) + " / " + formatSec(m_info.duration));

    if (m_info.duration <= 0.0) return;
    const int value = static_cast<int>(sec / m_info.duration * kSliderMax);
    QSignalBlocker block(m_seekSlider);
    m_seekSlider->setValue(value);
}

void MainWindow::onSetIn()
{
    m_inSec = sliderToSec(m_seekSlider->value());
    m_inSet = true;
    updateRangeMarkers();
}

void MainWindow::onSetOut()
{
    m_outSec = sliderToSec(m_seekSlider->value());
    m_outSet = true;
    updateRangeMarkers();
}

void MainWindow::onStop()
{
    if (!m_info.valid) return;

    m_videoView->pause();
    m_videoView->setPosition(0);

    m_inSet  = false;
    m_outSet = false;
    m_inSec  = 0.0;
    m_outSec = m_info.duration;
    updateRangeMarkers();
}

void MainWindow::onConvertOrCancel()
{
    // 変換中なら中止する
    if (m_encoder && m_encoder->isRunning()) {
        m_encoder->cancel();
        return;
    }

    // --- バリデーション ---
    if (m_ffmpegPath.isEmpty() || !QFile::exists(m_ffmpegPath)) {
        QMessageBox::warning(this, "設定エラー",
            "ffmpeg.exe のパスが正しく設定されていません。\n"
            "avply.toml を確認してください。");
        return;
    }
    if (m_filePath.isEmpty()) {
        QMessageBox::warning(this, "入力エラー", "動画ファイルを選択してください。");
        return;
    }
    if (m_info.duration <= 0.0) {
        QMessageBox::warning(this, "入力エラー", "動画の長さを取得できませんでした。");
        return;
    }
    const double effectiveIn  = m_inSet  ? m_inSec  : 0.0;
    const double effectiveOut = m_outSet ? m_outSec : m_info.duration;
    if (effectiveIn >= effectiveOut) {
        QMessageBox::warning(this, "範囲エラー", "開始は終了より前に設定してください。");
        return;
    }

    // NVENC AV1 対応確認
    if (!Ffmpeg::checkAv1Nvenc(m_ffmpegPath)) {
        QMessageBox::critical(this, "GPU エラー",
            "av1_nvenc エンコーダが利用できません。\n"
            "NVIDIA GPU と最新ドライバを確認してください。");
        return;
    }

    // 出力パスを生成
    const QString outputPath = OutputNamer::generate(m_filePath);

    EncodeParams params;
    params.inputPath    = m_filePath;
    params.outputPath   = outputPath;
    params.inSec        = effectiveIn;
    params.outSec       = effectiveOut;
    params.inputWidth   = m_info.width;
    params.inputBitrate = m_info.videoBitrate;

    // 旧 Encoder を破棄してから新規生成する
    if (m_encoder) {
        disconnect(m_encoder, nullptr, this, nullptr);
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_encoder = new Encoder(m_ffmpegPath, this);
    connect(m_encoder, &Encoder::progressChanged, this, &MainWindow::onEncoderProgress);
    connect(m_encoder, &Encoder::finished,        this, &MainWindow::onEncoderFinished);

    m_outputLabel->setText("  変換中です：" + outputPath);
    m_seekSlider->setProgress(0);
    setConverting(true);

    m_encoder->encode(params);
}

void MainWindow::onEncoderProgress(int pct)
{
    m_seekSlider->setProgress(pct);
}

void MainWindow::onEncoderFinished(bool ok, const QString& outputPath, const QString& err)
{
    setConverting(false);

    if (ok) {
        // 完了時は 100% で青を区間全体に重ねた状態を維持する
        m_seekSlider->setProgress(100);
        m_outputLabel->setText("  完了しました：" + outputPath);
        return;
    }

    // 中止・失敗時は進捗オーバーレイを除去して区間表示を元に戻す
    m_seekSlider->clearProgress();

    // ユーザ中止：err 空文字 → ダイアログ抑制
    if (err.isEmpty()) {
        m_outputLabel->setText("  中止しました");
        return;
    }

    m_outputLabel->setText("  失敗しました：" + err);
    QMessageBox::critical(this, "変換エラー", err);
}

// ---- 内部ユーティリティ ----

void MainWindow::loadFile(const QString& path)
{
    // QMediaPlayer の非同期ロードを ffprobe 実行と並行させて先頭フレーム表示を早める
    m_videoView->setSource(path);

    const QString ffprobePath = Ffmpeg::ffprobePath(m_ffmpegPath);
    FfmpegResult result;
    const VideoInfo info = Ffmpeg::probe(ffprobePath, path, result);
    if (!result.ok) {
        m_videoView->clear();
        QMessageBox::critical(this, "エラー", "動画情報を取得できませんでした：\n" + result.err);
        return;
    }
    if (!info.valid || info.duration <= 0.0) {
        m_videoView->clear();
        QMessageBox::critical(this, "エラー", "有効な動画ファイルではありません。");
        return;
    }

    m_filePath = path;
    m_info     = info;
    m_inSet    = false;
    m_outSet   = false;
    m_inSec    = 0.0;
    m_outSec   = info.duration;

    m_filePathLabel->setText(path);
    {
        QSignalBlocker block(m_seekSlider);
        m_seekSlider->setValue(0);
    }
    m_seekSlider->clearRangeMarkers();
    m_seekSlider->clearProgress();
    m_outputLabel->clear();
    m_posLabel->setText("  00:00:00 / " + formatSec(info.duration));

    // 動画情報をステータスバー左端に表示する
    // 形式：解像度  fps  映像コーデック ビットレート  音声コーデック ビットレート サンプリング ch
    QString videoInfo = QString("  %1x%2").arg(info.width).arg(info.height);
    if (info.frameRate > 0.0) {
        videoInfo += "  " + QString::number(info.frameRate, 'g', 4) + "fps";
    }
    if (!info.codec.isEmpty()) {
        videoInfo += "  " + info.codec;
        if (info.videoBitrate > 0.0) {
            videoInfo += " " + QString::number(info.videoBitrate / 1.0e6, 'f', 1) + "Mbps";
        }
    }
    if (!info.audioCodec.isEmpty()) {
        videoInfo += "  " + info.audioCodec;
        if (info.audioBitrate > 0.0) {
            videoInfo += " " + QString::number(static_cast<int>(info.audioBitrate / 1000.0)) + "kbps";
        }
        if (info.audioSampleRate > 0) {
            videoInfo += " " + QString::number(info.audioSampleRate / 1000.0, 'g', 3) + "kHz";
        }
        if (info.audioChannels > 0) {
            videoInfo += " " + QString::number(info.audioChannels) + "ch";
        }
    }
    m_videoInfoLabel->setText(videoInfo);

    // 動画読込が完了したのでファイル依存ボタンをまとめて活性化する
    setUiEnabled(true);

    // 新規 QMediaPlayer ソースに現在の再生速度を改めて適用する
    // 再生速度はインスタンス起動中ずっと保持するためファイル間でリセットしない
    m_videoView->setPlaybackRate(m_playbackRate);

    // 動画のアスペクト比をウィンドウ連動の基準として更新する
    m_videoAspect = static_cast<double>(info.width) / info.height;
    updateMinimumWindowSize();

    // モニタ作業領域の指定比率を上限としてアスペクト比維持で動画サイズを縮める
    // 比率は avply.toml の [window].initial_screen_ratio で変更可能（デフォルト 0.8）
    const QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geom = sc->availableGeometry();
    const double maxWindowW  = geom.width()  * m_initialScreenRatio;
    const double maxWindowH  = geom.height() * m_initialScreenRatio;
    const double maxPreviewH = maxWindowH - m_lowerUiH;

    // 元動画サイズに対するスケール係数（1.0 を超えない範囲で最も小さい制約を採用）
    double scale = 1.0;
    if (info.width > maxWindowW) {
        scale = std::min(scale, maxWindowW / info.width);
    }
    if (maxPreviewH > 0 && info.height > maxPreviewH) {
        scale = std::min(scale, maxPreviewH / info.height);
    }

    const int previewW = qRound(info.width  * scale);
    const int previewH = qRound(info.height * scale);

    m_resizingProgrammatically = true;
    resize(std::max(400, previewW), previewH + m_lowerUiH);
    // タイトルバーを含むフレーム矩形をモニタ作業領域の中心に合わせる
    // frameGeometry は resize 直後も Windows では即時反映されるため安全
    QRect frame = frameGeometry();
    frame.moveCenter(geom.center());
    move(frame.topLeft());
    m_resizingProgrammatically = false;
}

bool MainWindow::isAcceptedVideo(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "mp4" || ext == "mkv" || ext == "mov"
        || ext == "avi" || ext == "webm";
}

void MainWindow::setUiEnabled(bool enabled)
{
    // ファイル依存ボタンは「enabled かつ動画読込済」のときのみ活性化する
    const bool fileLoaded = enabled && m_info.valid;
    const bool ffmpegOk = !m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath);
    m_openBtn->setEnabled(enabled);
    m_seekSlider->setEnabled(fileLoaded);
    m_playPauseBtn->setEnabled(fileLoaded);
    m_stopBtn->setEnabled(fileLoaded);
    m_setInBtn->setEnabled(fileLoaded);
    m_setOutBtn->setEnabled(fileLoaded);
    m_convertBtn->setEnabled(fileLoaded && ffmpegOk);
}

void MainWindow::setConverting(bool converting)
{
    setUiEnabled(!converting);
    // 変換中は D&D を拒否する（ウィンドウ全体・プレビュー領域の両方）
    setAcceptDrops(!converting);
    m_videoView->setAcceptDrops(!converting);
    m_convertBtn->setText(converting ? "中止" : "変換");
    // 変換中は中止ボタンとして convertBtn のみ活性に保つ
    if (converting) m_convertBtn->setEnabled(true);
}

void MainWindow::updateMinimumWindowSize()
{
    constexpr int kMinW = 400;
    const int minH = qRound(kMinW / m_videoAspect) + m_lowerUiH;
    setMinimumSize(kMinW, minH);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // 動画未読込時は m_videoAspect がデフォルト 16:9 のため矯正しない
    if (m_resizingProgrammatically || m_lowerUiH <= 0 || !m_info.valid) return;
    // 最大化・全画面・最小化中は OS にサイズ制御を任せ、アスペクト矯正を行わない
    // （矯正で resize すると画面領域を超えて下部 UI が画面外に押し出される）
    if (windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) return;

    // 幅基準で「正しい高さ」を逆算してウィンドウのアスペクト比を矯正する
    const int targetH = qRound(width() / m_videoAspect) + m_lowerUiH;
    if (targetH != height()) {
        m_resizingProgrammatically = true;
        resize(width(), targetH);
        m_resizingProgrammatically = false;
    }
}

void MainWindow::updateRangeMarkers()
{
    // 区間が変わったら過去の進捗オーバーレイは無効になる
    m_seekSlider->clearProgress();

    if ((!m_inSet && !m_outSet) || m_info.duration <= 0.0) {
        m_seekSlider->clearRangeMarkers();
        return;
    }
    const double effectiveIn  = m_inSet  ? m_inSec  : 0.0;
    const double effectiveOut = m_outSet ? m_outSec : m_info.duration;
    const double inRatio  = effectiveIn  / m_info.duration;
    const double outRatio = effectiveOut / m_info.duration;
    m_seekSlider->setRangeMarkers(inRatio, outRatio);
}

double MainWindow::sliderToSec(int value) const
{
    if (m_info.duration <= 0.0) return 0.0;
    return static_cast<double>(value) / kSliderMax * m_info.duration;
}

QString MainWindow::formatSec(double sec)
{
    if (sec < 0.0) sec = 0.0;
    const int total = static_cast<int>(sec);
    return QString("%1:%2:%3")
        .arg(total / 3600,          2, 10, QChar('0'))
        .arg((total % 3600) / 60,   2, 10, QChar('0'))
        .arg(total % 60,            2, 10, QChar('0'));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        // モーダルダイアログ表示中は素通しする（ファイル選択や警告ダイアログを誤動作させない）
        if (QApplication::activeModalWidget()) {
            return QMainWindow::eventFilter(watched, event);
        }
        const auto* ke = static_cast<QKeyEvent*>(event);
        switch (ke->key()) {
        case Qt::Key_Left:
            seekRelative(-m_seekLeftMs);
            return true;
        case Qt::Key_Right:
            seekRelative(m_seekRightMs);
            return true;
        case Qt::Key_Space:
            if (m_info.duration > 0.0) m_videoView->togglePlay();
            return true;
        case Qt::Key_Up:
            changePlaybackRate(0.05);
            return true;
        case Qt::Key_Down:
            changePlaybackRate(-0.05);
            return true;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::seekRelative(int deltaMs)
{
    if (m_info.duration <= 0.0 || deltaMs == 0) return;
    const qint64 durationMs = static_cast<qint64>(m_info.duration * 1000.0);
    const qint64 newPos = qBound(qint64(0), m_videoView->position() + deltaMs, durationMs);
    m_videoView->setPosition(newPos);
}

void MainWindow::changePlaybackRate(qreal delta)
{
    if (m_info.duration <= 0.0) return;
    // 浮動小数点の累積誤差を抑えるため 0.05 単位に丸める
    const qreal next = std::round((m_playbackRate + delta) * 100.0) / 100.0;
    m_playbackRate = qBound(qreal(0.05), next, qreal(4.0));
    m_videoView->setPlaybackRate(m_playbackRate);
    updateSpeedDisplay();
}

void MainWindow::updateSpeedDisplay()
{
    m_speedLabel->setText(QString::asprintf("  x%.2f", m_playbackRate));
}

QString MainWindow::openDialogStartDir() const
{
    if (!m_filePath.isEmpty()) {
        return QFileInfo(m_filePath).absolutePath();
    }
    return QDir::homePath();
}

void MainWindow::validateFfmpegPath()
{
    if (!m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath)) return;

    // 変換ボタンの活性は setUiEnabled が QFile::exists で都度判定するためここでは状態を持たない
    QMessageBox::warning(this, "設定エラー",
        "ffmpeg.exe のパスが見つかりません。\n"
        "実行ファイルと同階層の avply.toml に\n"
        "  [ffmpeg]\n"
        "  path = \"<ffmpeg.exe へのパス>\"\n"
        "を設定してから起動し直してください。");
}
