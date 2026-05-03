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
#include <QMimeData>
#include <QUrl>
#include <QSignalBlocker>
#include <QTimer>
#include <QStatusBar>
#include <QKeyEvent>
#include <QApplication>

static constexpr int kSliderMax = 10000;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_encoder(nullptr)
{
    setWindowTitle("vcutter");
    setFixedWidth(static_cast<int>(1080 * 0.8)); // 864
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
    m_posLabel = new QLabel("--:--:-- / --:--:--");

    // --- シークスライダー ---
    m_seekSlider = new RangeSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, kSliderMax);
    m_seekSlider->setEnabled(false);
    // valueChanged を使うことでクリックジャンプの位置も拾える
    connect(m_seekSlider, &QSlider::valueChanged,
            this, &MainWindow::onSeekSliderChanged);

    // --- 開始/終了 設定行 ---
    m_setInBtn  = new QPushButton("開始を設定");
    m_inLabel   = new QLabel("開始：未設定");
    m_setOutBtn = new QPushButton("終了を設定");
    m_outLabel  = new QLabel("終了：未設定");
    m_setInBtn->setEnabled(false);
    m_setOutBtn->setEnabled(false);
    connect(m_setInBtn,  &QPushButton::clicked, this, &MainWindow::onSetIn);
    connect(m_setOutBtn, &QPushButton::clicked, this, &MainWindow::onSetOut);

    auto* inOutRow = new QHBoxLayout;
    inOutRow->addWidget(m_setInBtn);
    inOutRow->addWidget(m_inLabel);
    inOutRow->addStretch();
    inOutRow->addWidget(m_setOutBtn);
    inOutRow->addWidget(m_outLabel);

    // --- 変換行（プログレスバー + トグルボタン） ---
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_convertBtn = new QPushButton("変換");
    m_convertBtn->setFixedWidth(120);
    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::onConvertOrCancel);

    auto* convertRow = new QHBoxLayout;
    convertRow->addWidget(m_progressBar, 1);
    convertRow->addWidget(m_convertBtn);

    // --- 出力ファイルラベル（ステータスバー左に配置） ---
    m_outputLabel = new QLabel;
    m_outputLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // --- メインレイアウト ---
    auto* central = new QWidget;
    auto* main    = new QVBoxLayout(central);
    main->setSpacing(8);
    main->setContentsMargins(12, 12, 12, 12);
    main->addLayout(fileRow);
    main->addWidget(m_videoView);
    main->addWidget(m_seekSlider);
    main->addLayout(inOutRow);
    main->addLayout(convertRow);

    setCentralWidget(central);

    // --- ステータスバー：左に出力状況、右に再生位置 ---
    statusBar()->addWidget(m_outputLabel, 1);
    statusBar()->addPermanentWidget(m_posLabel);

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
    m_ffmpegPath  = cfg.ffmpegPath;
    m_seekLeftMs  = cfg.seekLeftMs;
    m_seekRightMs = cfg.seekRightMs;
    // アプリケーション全体のキー入力を捕捉して左右カーソルシークに変換する
    qApp->installEventFilter(this);
    // ウィンドウ表示後に検証する（show 前のダイアログ表示を避ける）
    QTimer::singleShot(0, this, &MainWindow::validateFfmpegPath);
    // レイアウト確定後に高さを中身基準で固定する
    QTimer::singleShot(0, this, [this]() {
        adjustSize();
        setFixedHeight(height());
    });
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
        this, "動画ファイルを開く", {},
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
    m_posLabel->setText(formatSec(sec) + " / " + formatSec(m_info.duration));

    if (m_info.duration <= 0.0) return;
    const int value = static_cast<int>(sec / m_info.duration * kSliderMax);
    QSignalBlocker block(m_seekSlider);
    m_seekSlider->setValue(value);
}

void MainWindow::onSetIn()
{
    m_inSec = sliderToSec(m_seekSlider->value());
    m_inSet = true;
    m_inLabel->setText("開始：" + formatSec(m_inSec));
    updateRangeMarkers();
}

void MainWindow::onSetOut()
{
    m_outSec = sliderToSec(m_seekSlider->value());
    m_outSet = true;
    m_outLabel->setText("終了：" + formatSec(m_outSec));
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
            "vcutter.toml を確認してください。");
        return;
    }
    if (m_filePath.isEmpty()) {
        QMessageBox::warning(this, "入力エラー", "動画ファイルを選択してください。");
        return;
    }
    if (!m_inSet || !m_outSet) {
        QMessageBox::warning(this, "範囲エラー", "開始と終了を両方設定してください。");
        return;
    }
    if (m_inSec >= m_outSec) {
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
    params.inSec        = m_inSec;
    params.outSec       = m_outSec;
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

    m_outputLabel->setText("変換中です：" + outputPath);
    m_progressBar->setValue(0);
    setConverting(true);

    m_encoder->encode(params);
}

void MainWindow::onEncoderProgress(int pct)
{
    m_progressBar->setValue(pct);
}

void MainWindow::onEncoderFinished(bool ok, const QString& outputPath, const QString& err)
{
    setConverting(false);

    if (ok) {
        m_progressBar->setValue(100);
        m_outputLabel->setText("完了しました：" + outputPath);
        return;
    }

    // ユーザ中止：err 空文字 → ダイアログ抑制
    if (err.isEmpty()) {
        m_progressBar->setValue(0);
        m_outputLabel->setText("中止しました");
        return;
    }

    m_progressBar->setValue(0);
    m_outputLabel->setText("失敗しました：" + err);
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
    m_seekSlider->setEnabled(true);
    m_seekSlider->clearRangeMarkers();
    m_setInBtn->setEnabled(true);
    m_setOutBtn->setEnabled(true);
    m_inLabel->setText("開始：未設定");
    m_outLabel->setText("終了：未設定");
    m_outputLabel->clear();
    m_posLabel->setText("00:00:00 / " + formatSec(info.duration));
}

bool MainWindow::isAcceptedVideo(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "mp4" || ext == "mkv" || ext == "mov"
        || ext == "avi" || ext == "webm";
}

void MainWindow::setUiEnabled(bool enabled)
{
    m_openBtn->setEnabled(enabled);
    m_seekSlider->setEnabled(enabled && m_info.valid);
    m_setInBtn->setEnabled(enabled && m_info.valid);
    m_setOutBtn->setEnabled(enabled && m_info.valid);
}

void MainWindow::setConverting(bool converting)
{
    setUiEnabled(!converting);
    // 変換中は D&D を拒否する（ウィンドウ全体・プレビュー領域の両方）
    setAcceptDrops(!converting);
    m_videoView->setAcceptDrops(!converting);
    m_convertBtn->setText(converting ? "中止" : "変換");
}

void MainWindow::updateRangeMarkers()
{
    if (!m_inSet || !m_outSet || m_info.duration <= 0.0) {
        m_seekSlider->clearRangeMarkers();
        return;
    }
    const double inRatio  = m_inSec  / m_info.duration;
    const double outRatio = m_outSec / m_info.duration;
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
        const auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Left) {
            seekRelative(-m_seekLeftMs);
            return true;
        }
        if (ke->key() == Qt::Key_Right) {
            seekRelative(m_seekRightMs);
            return true;
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

void MainWindow::validateFfmpegPath()
{
    if (!m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath)) return;

    m_convertBtn->setEnabled(false);
    QMessageBox::warning(this, "設定エラー",
        "ffmpeg.exe のパスが見つかりません。\n"
        "実行ファイルと同階層の vcutter.toml に\n"
        "  [ffmpeg]\n"
        "  path = \"<ffmpeg.exe へのパス>\"\n"
        "を設定してから起動し直してください。");
}
