#include "MainWindow.h"
#include "Config.h"
#include "OutputNamer.h"
#include "Settings.h"
#include <QApplication>
#include <QEventLoop>
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
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#include <QCryptographicHash>
#include <QAction>
#include <QMenu>
#include <QContextMenuEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

// WM_SIZING / WMSZ_* 定数のため Windows API ヘッダを取り込む
// NOMINMAX を先に定義しないと windows.h の min / max マクロが std::min / std::max と衝突する
#define NOMINMAX
#include <windows.h>

static constexpr int kSliderMax = 10000;

// 起動時の初期ウィンドウサイズ（最小サイズも兼ねる）
// 動画ロード後に動画サイズへリサイズするまでの暫定表示用
static constexpr int kInitialWindowW = 500;
static constexpr int kInitialWindowH = 375;

// 音声波形 PNG の生成サイズ
// シークバー幅は最大でも数百 px だが、QPainter 側のスケール描画品質を保つため幅 2048px の余裕を持たせる。
// 高さ 48px はトラック高 28px への縮小描画でも詳細が潰れない解像度
static constexpr int kWaveformW = 2048;
static constexpr int kWaveformH = 48;

MainWindow::MainWindow(const QString& initialPath, QWidget* parent)
    : QMainWindow(parent)
    , m_encoder(nullptr)
{
    setWindowTitle("avply");
    setAcceptDrops(true);

    // リサイズで露出した領域の未描画ギャップを軽減する
    // 新たに露出した領域がパレット既定色で即時クリアされ、「外枠だけ新サイズ・内側未描画」の
    // 描画追従ラグによる隙間が見えにくくなる
    setAutoFillBackground(true);

    // --- ファイル名表示ラベル（旧「開く...」ボタンはコンテキストメニューに移行済み） ---
    m_filePathLabel = new QLabel("メディアファイルを選択するか、ウィンドウへドロップしてください");
    m_filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // --- 動画プレビュー（クリックで再生/停止トグル、D&D でファイル読み込み） ---
    m_videoView = new VideoView;
    connect(m_videoView, &VideoView::positionChanged,
            this, &MainWindow::onPlayerPositionChanged);
    connect(m_videoView, &VideoView::fileDropped,
            this, [this](const QString& path) {
        if (isAcceptedMedia(path)) loadFile(path);
    });
    // プレビュー領域のホイール回転をシークに変換する（変換中・対象方向が無効値の場合は抑制）
    connect(m_videoView, &VideoView::wheelScrolled, this, [this](bool forward) {
        if (m_runningOp != Operation::None) return;
        const int ms = forward ? m_seekWheelForwardMs : m_seekWheelBackMs;
        if (ms > 0) seekRelative(forward ? ms : -ms);
    });

    // --- 再生位置ラベル（ステータスバー右端に配置） ---
    // 先頭 2 半角スペースは項目間の区切りとして機能する
    m_posLabel = new QLabel("  --:--:-- / --:--:--");

    // --- 再生速度ラベル（ステータスバー右端、再生位置の右に配置） ---
    m_speedLabel = new QLabel("  x1.00");

    // --- 音量ブースト倍率ラベル（再生速度の右に配置） ---
    // 値は avply.toml から取得し、起動時に一度だけ設定する
    m_volumeLabel = new QLabel("  x1.00");

    // --- シークスライダー ---
    m_seekSlider = new RangeSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, kSliderMax);
    m_seekSlider->setEnabled(false);
    // valueChanged を使うことでクリックジャンプの位置も拾える
    connect(m_seekSlider, &QSlider::valueChanged,
            this, &MainWindow::onSeekSliderChanged);
    // ホイール回転をシークに変換する（変換中・対象方向が無効値の場合は抑制）
    connect(m_seekSlider, &RangeSlider::wheelScrolled, this, [this](bool forward) {
        if (m_runningOp != Operation::None) return;
        const int ms = forward ? m_seekWheelForwardMs : m_seekWheelBackMs;
        if (ms > 0) seekRelative(forward ? ms : -ms);
    });

    // アイコン式ボタン共通スタイル
    // 外枠と内側パディングを消し、ホバー時のみ薄いグレーで反応を示す
    // padding: 0 を入れないとテキストボタン（【】）でホバー範囲が縦に膨らみ、
    // アイコンボタン（再生・停止）と見た目のサイズが揃わない
    const QString iconBtnStyle =
        "QPushButton { border: none; padding: 0; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 30); }";
    // アイコン式ボタンの統一サイズ（再生・停止・【・】 すべて同じ矩形でホバーする）
    // 【】テキストの見た目に揃うようコンパクトにし、アイコンも一回り小さくする
    const QSize iconBtnSize(28, 28);
    const QSize iconImgSize(18, 18);

    // --- 再生/一時停止ボタン（シークバー左、再生状態の視認も兼ねる） ---
    // PNG アイコンを使用する
    m_iconPlay  = QIcon(":/icons/play.png");
    m_iconPause = QIcon(":/icons/pause.png");
    m_playPauseBtn = new QPushButton;
    m_playPauseBtn->setIcon(m_iconPlay);
    m_playPauseBtn->setIconSize(iconImgSize);
    connect(m_playPauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_info.valid) m_videoView->togglePlay();
    });
    connect(m_videoView, &VideoView::playbackStateChanged,
            this, [this](bool playing) {
        m_playPauseBtn->setIcon(playing ? m_iconPause : m_iconPlay);
        m_isPlaying = playing;
        applyTopmostState();
    });

    // --- 停止ボタン（シーク位置を 0 に戻し、開始/終了マーカーをクリアする） ---
    m_stopBtn = new QPushButton;
    m_stopBtn->setIcon(QIcon(":/icons/stop.png"));
    m_stopBtn->setIconSize(iconImgSize);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    // --- 開始/終了 設定ボタン（再生/停止と同じアイコン式スタイルに揃える） ---
    // [ / ] キーでも操作できるようキーボードショートカットを割り当てる
    m_setInBtn  = new QPushButton("【");
    m_setOutBtn = new QPushButton("】");
    m_setInBtn ->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    m_setOutBtn->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(m_setInBtn,  &QPushButton::clicked, this, &MainWindow::onSetIn);
    connect(m_setOutBtn, &QPushButton::clicked, this, &MainWindow::onSetOut);

    // 4 つのアイコン式ボタンに共通スタイルとサイズを一括適用する
    for (QPushButton* b : { m_playPauseBtn, m_stopBtn, m_setInBtn, m_setOutBtn }) {
        b->setStyleSheet(iconBtnStyle);
        b->setFixedSize(iconBtnSize);
        b->setEnabled(false);
    }

    // --- トリムボタン（シークバー行の右側に配置する。「変換」はコンテキストメニュー側に移行済み） ---
    m_trimBtn = new QPushButton("トリム");
    m_trimBtn->setFixedWidth(64);
    m_trimBtn->setEnabled(false);
    connect(m_trimBtn, &QPushButton::clicked, this, &MainWindow::onTrimOrCancel);

    // 左側アイコン群を内側レイアウトでまとめ、ボタン同士をピッタリ隣接させる
    auto* leftIconRow = new QHBoxLayout;
    leftIconRow->setSpacing(0);
    leftIconRow->setContentsMargins(0, 0, 0, 0);
    leftIconRow->addWidget(m_playPauseBtn);
    leftIconRow->addWidget(m_stopBtn);
    leftIconRow->addWidget(m_setInBtn);

    // 行内すべての要素を縦中央で揃える
    // ボタン高（28px）とスライダーの sizeHint 高に差があるためウィジェット間で
    // 中心位置がズレやすい。AlignVCenter を明示することでウィンドウリサイズ時も
    // シークバーが各ボタンの中心と一致した状態を保つ
    auto* seekRow = new QHBoxLayout;
    seekRow->setSpacing(2);
    seekRow->addLayout(leftIconRow);
    seekRow->setAlignment(leftIconRow, Qt::AlignVCenter);
    seekRow->addWidget(m_seekSlider, 1, Qt::AlignVCenter);
    seekRow->addWidget(m_setOutBtn,  0, Qt::AlignVCenter);
    seekRow->addWidget(m_trimBtn,    0, Qt::AlignVCenter);

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
    main->addWidget(m_filePathLabel);
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
    statusBar()->addPermanentWidget(m_volumeLabel);

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
    m_seekWheelForwardMs   = cfg.wheelForwardMs;
    m_seekWheelBackMs      = cfg.wheelBackMs;
    m_initialScreenRatio   = cfg.initialScreenRatio;
    m_playbackRate = cfg.playbackSpeed;
    m_videoView->setVolumeBoost(cfg.audioVolume);
    m_volumeLabel->setText(QString::asprintf("  x%.2f", cfg.audioVolume));
    updateSpeedDisplay();

    // --- コンテキストメニュー用アクションを構築する ---
    // contextMenuEvent ごとにメニューを組み立てる際に使い回せるようメンバとして保持する
    m_actOpen = new QAction("ファイルを開く", this);
    connect(m_actOpen, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_actConvert = new QAction("ファイルを変換する", this);
    connect(m_actConvert, &QAction::triggered, this, &MainWindow::onConvertOrCancel);

    m_actTrim = new QAction("ファイルをトリムする", this);
    connect(m_actTrim, &QAction::triggered, this, &MainWindow::onTrimOrCancel);

    m_actTopmost = new QAction("再生中は常に最前面に表示する", this);
    m_actTopmost->setCheckable(true);
    m_actTopmost->setChecked(Settings::instance().topmostWhilePlaying());
    connect(m_actTopmost, &QAction::toggled, this, &MainWindow::onToggleTopmost);

    m_actSingleInst = new QAction("常にひとつのプレイヤーで再生する", this);
    m_actSingleInst->setCheckable(true);
    m_actSingleInst->setChecked(Settings::instance().singleInstance());
    m_actSingleInst->setToolTip("変更は次回起動から有効");
    connect(m_actSingleInst, &QAction::toggled, this, &MainWindow::onToggleSingleInstance);

    m_actPriority = new QAction("プロセス優先度を通常以上にする", this);
    m_actPriority->setCheckable(true);
    m_actPriority->setChecked(Settings::instance().aboveNormalPriority());
    connect(m_actPriority, &QAction::toggled, this, &MainWindow::onTogglePriority);

    updateMenuActionEnabled();
    // アプリケーション全体のキー入力を捕捉して左右カーソルシークに変換する
    qApp->installEventFilter(this);

    // show() 前にレイアウトを確定して下部 UI 高（プレビューを除いた UI 部の高さ）を保存する
    adjustSize();
    m_lowerUiH = height() - m_videoView->height();

    const bool hasInitialPath = !initialPath.isEmpty()
        && isAcceptedMedia(initialPath) && QFile::exists(initialPath);

    if (hasInitialPath && isAudioByExtension(initialPath)) {
        // 音声ファイルは可視化前にプレビュー領域を非表示にして幅 500 で見せる。
        // 最小高も m_lowerUiH に合わせ、起動直後から最終形に近い見た目で表示する
        m_videoView->hide();
        setMinimumSize(kInitialWindowW, m_lowerUiH);
        resize(kInitialWindowW, m_lowerUiH);
    }
    else {
        // 動画／ファイル指定なし共通：500x375 を初期形とする
        setMinimumSize(kInitialWindowW, kInitialWindowH);
        resize(kInitialWindowW, kInitialWindowH);
    }

    // 初期ファイルのロードはイベントループに戻った直後に行い、show() を最速で先行させる
    // これにより、ユーザにはまずデフォルトサイズのウィンドウが表示され、続いて動画サイズへリサイズされる
    if (hasInitialPath) {
        QTimer::singleShot(0, this, [this, initialPath]() { loadFile(initialPath); });
    }

    // ウィンドウ表示後に検証する（show 前のダイアログ表示を避ける）
    QTimer::singleShot(0, this, &MainWindow::validateFfmpegPath);
}

MainWindow::~MainWindow()
{
    // デストラクタ実行中にコールバックが発火すると this が破棄済みとなり未定義動作になるため、
    // synchronous=true で waitForFinished を挟んで確実に終わらせる
    stopWaveformProcess(true);
}

// ---- ドラッグ＆ドロップ ----

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isAcceptedMedia(url.toLocalFile())) {
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
        if (isAcceptedMedia(path)) {
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
        this, "メディアファイルを開く", openDialogStartDir(),
        "メディアファイル (*.mp4 *.mkv *.mov *.avi *.webm *.mp3 *.wav *.flac *.ogg *.opus)"
        ";;すべてのファイル (*)");
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
    startOrCancel(EncodeMode::Reencode);
}

void MainWindow::onTrimOrCancel()
{
    startOrCancel(EncodeMode::StreamCopy);
}

void MainWindow::startOrCancel(EncodeMode mode)
{
    // 同モード実行中なら中止する（異モード実行中はボタン非活性で到達しない想定）
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
        QMessageBox::warning(this, "入力エラー", "メディアファイルを選択してください。");
        return;
    }
    if (m_info.duration <= 0.0) {
        QMessageBox::warning(this, "入力エラー", "メディアの長さを取得できませんでした。");
        return;
    }

    // IN/OUT 未指定なら全長を自動指定する
    // 中断時に赤バーを残すことで実際に処理対象だった範囲をユーザに示す
    if (!m_inSet) {
        m_inSec = 0.0;
        m_inSet = true;
    }
    if (!m_outSet) {
        m_outSec = m_info.duration;
        m_outSet = true;
    }
    updateRangeMarkers();

    const double effectiveIn  = m_inSec;
    const double effectiveOut = m_outSec;
    if (effectiveIn >= effectiveOut) {
        QMessageBox::warning(this, "範囲エラー", "開始は終了より前に設定してください。");
        return;
    }

    // 動画の再エンコードは NVENC を使うため対応確認を行う。音声のみは libopus のみで NVENC 不要
    if (mode == EncodeMode::Reencode && !isAudioOnly()) {
        if (!Ffmpeg::checkAv1Nvenc(m_ffmpegPath)) {
            QMessageBox::critical(this, "GPU エラー",
                "av1_nvenc エンコーダが利用できません。\n"
                "NVIDIA GPU と最新ドライバを確認してください。");
            return;
        }
    }

    // 出力拡張子を決定する：
    //   変換 + 動画 → mp4、変換 + 音声 → opus、トリム → 入力拡張子を維持
    QString outExt;
    if (mode == EncodeMode::Reencode) {
        outExt = isAudioOnly() ? "opus" : "mp4";
    }
    else {
        outExt = QFileInfo(m_filePath).suffix().toLower();
    }
    const QString outputPath = OutputNamer::generate(m_filePath, outExt);

    EncodeParams params;
    params.mode         = mode;
    params.inputPath    = m_filePath;
    params.outputPath   = outputPath;
    params.inSec        = effectiveIn;
    params.outSec       = effectiveOut;
    params.inputWidth   = m_info.width;
    params.hasVideo     = !isAudioOnly();

    // 旧 Encoder を破棄してから新規生成する
    if (m_encoder) {
        disconnect(m_encoder, nullptr, this, nullptr);
        m_encoder->deleteLater();
        m_encoder = nullptr;
    }
    m_encoder = new Encoder(m_ffmpegPath, this);
    connect(m_encoder, &Encoder::progressChanged, this, &MainWindow::onEncoderProgress);
    connect(m_encoder, &Encoder::finished,        this, &MainWindow::onEncoderFinished);

    const Operation op = (mode == EncodeMode::StreamCopy) ? Operation::Trim : Operation::Convert;
    const QString label = (op == Operation::Trim) ? "トリム中" : "変換中";
    m_outputLabel->setText(QString("  %1：0%").arg(label));
    m_seekSlider->setProgress(0);
    setRunning(op);

    m_encoder->encode(params);
}

void MainWindow::onEncoderProgress(int pct)
{
    m_seekSlider->setProgress(pct);
    const QString label = (m_runningOp == Operation::Trim) ? "トリム中" : "変換中";
    m_outputLabel->setText(QString("  %1：%2%").arg(label).arg(pct));
}

void MainWindow::onEncoderFinished(bool ok, const QString& outputPath, const QString& err)
{
    setRunning(Operation::None);

    if (ok) {
        // 完了時は 100% で青を区間全体に重ねた状態を維持する
        m_seekSlider->setProgress(100);
        m_outputLabel->setText("  完了しました：" + outputPath);
        return;
    }

    // 中止・失敗時は進捗オーバーレイを除去して区間表示を元に戻す
    m_seekSlider->clearProgress();

    // ユーザ中止：err 空文字 → ダイアログ抑制、ステータス表示もクリアのみ
    // 進捗オーバーレイの消失で中止は十分認識可能
    if (err.isEmpty()) {
        m_outputLabel->clear();
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
        QMessageBox::critical(this, "エラー", "有効なメディアファイルではありません。");
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

    // メディア情報をステータスバー左端に表示する
    // 動画形式：解像度  fps  映像コーデック ビットレート  音声コーデック ビットレート サンプリング ch
    // 音声のみ：先頭の解像度・fps・映像コーデック表示は省略する
    QString videoInfo;
    if (!isAudioOnly()) {
        videoInfo = QString("  %1x%2").arg(info.width).arg(info.height);
        if (info.frameRate > 0.0) {
            videoInfo += "  " + QString::number(info.frameRate, 'g', 4) + "fps";
        }
        if (!info.codec.isEmpty()) {
            videoInfo += "  " + info.codec;
            if (info.videoBitrate > 0.0) {
                videoInfo += " " + QString::number(info.videoBitrate / 1.0e6, 'f', 1) + "Mbps";
            }
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

    // 読込完了に応じて動画プレビュー領域の表示／非表示を切り替える
    // 音声のみ：プレビュー領域を完全に消し、下部 UI のみのコンパクト表示にする
    // 動画あり：QVideoWidget の遅延表示は VideoView 内部のロジックに委ねる
    if (isAudioOnly()) {
        m_videoView->hide();
    }
    else {
        m_videoView->show();
    }

    // 読込が完了したのでファイル依存ボタンをまとめて活性化する
    setUiEnabled(true);

    // 音声波形を非同期生成する。音声ストリームが無いファイルは中央基線で代替する
    m_seekSlider->clearWaveform();
    if (!info.hasAudio()) {
        m_seekSlider->setBaseline(true);
    }
    else {
        startWaveformGeneration(path);
    }

    // 新規 QMediaPlayer ソースに現在の再生速度を改めて適用する
    // 再生速度はインスタンス起動中ずっと保持するためファイル間でリセットしない
    m_videoView->setPlaybackRate(m_playbackRate);

    // ウィンドウサイズを決定する：動画はアスペクト比連動、音声は下部 UI 高にあわせる
    const QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect geom = sc->availableGeometry();

    if (isAudioOnly()) {
        // 音声専用：プレビュー領域がないため縦サイズを下部 UI 高に固定する
        // setFixedHeight は最小・最大の双方を同値に設定するため、Qt が WM_GETMINMAXINFO 経由で
        // OS にこの制約を伝え、Windows 自身がウィンドウの縦ドラッグを禁止する
        setMinimumWidth(kInitialWindowW);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setFixedHeight(m_lowerUiH);

        resize(std::max(kInitialWindowW, width()), m_lowerUiH);
        QRect frame = frameGeometry();
        frame.moveCenter(geom.center());
        move(frame.topLeft());
    }
    else {
        // 動画のアスペクト比をウィンドウ連動の基準として更新する
        m_videoAspect = static_cast<double>(info.width) / info.height;

        // 音声モードからの切替で残った setFixedHeight を解除する
        // setMinimumSize だけでは setFixedHeight が設定した最大高さが残るため、
        // 明示的に setMaximumHeight を呼んで縦伸縮を解放する必要がある
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumSize(kInitialWindowW, kInitialWindowH);

        // モニタ作業領域の指定比率を上限としてアスペクト比維持で動画サイズを縮める
        // 比率は avply.toml の [window].initial_screen_ratio で変更可能（デフォルト 0.8）
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

        resize(std::max(kInitialWindowW, previewW), previewH + m_lowerUiH);
        // タイトルバーを含むフレーム矩形をモニタ作業領域の中心に合わせる
        // frameGeometry は resize 直後も Windows では即時反映されるため安全
        QRect frame = frameGeometry();
        frame.moveCenter(geom.center());
        move(frame.topLeft());
    }
}

bool MainWindow::isAcceptedMedia(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp4" || ext == "mkv" || ext == "mov"
        || ext == "avi" || ext == "webm") return true;
    if (ext == "mp3" || ext == "wav" || ext == "flac"
        || ext == "ogg" || ext == "opus") return true;
    return false;
}

bool MainWindow::isAudioByExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "mp3" || ext == "wav" || ext == "flac"
        || ext == "ogg" || ext == "opus";
}

void MainWindow::setUiEnabled(bool enabled)
{
    // ファイル依存ボタンは「enabled かつ動画読込済」のときのみ活性化する
    const bool fileLoaded = enabled && m_info.valid;
    const bool ffmpegOk = !m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath);
    m_seekSlider->setEnabled(fileLoaded);
    m_playPauseBtn->setEnabled(fileLoaded);
    m_stopBtn->setEnabled(fileLoaded);
    m_setInBtn->setEnabled(fileLoaded);
    m_setOutBtn->setEnabled(fileLoaded);
    m_trimBtn   ->setEnabled(fileLoaded && ffmpegOk && isTrimMeaningful());
    updateMenuActionEnabled();
}

void MainWindow::updateMenuActionEnabled()
{
    // 「開く」は実行中以外（m_runningOp==None）なら常に許可
    // 「変換」「トリム」はファイル読込済 + ffmpeg 存在を要求し、トリムはさらに範囲が有効である必要がある
    const bool idle      = (m_runningOp == Operation::None);
    const bool ffmpegOk  = !m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath);
    const bool fileReady = m_info.valid;

    if (m_actOpen)    m_actOpen->setEnabled(idle);
    if (m_actConvert) {
        // 実行中（変換のみ）は中止操作のため有効のまま
        const bool running = (m_runningOp == Operation::Convert);
        m_actConvert->setEnabled(running || (idle && fileReady && ffmpegOk));
    }
    if (m_actTrim) {
        const bool running = (m_runningOp == Operation::Trim);
        m_actTrim->setEnabled(running || (idle && fileReady && ffmpegOk && isTrimMeaningful()));
    }
}

bool MainWindow::isTrimMeaningful() const
{
    // 実効範囲（IN/OUT 未指定なら全長）が動画全長と一致する場合、トリムは無意味
    // 浮動小数比較は秒数のため数 ms 程度の誤差を吸収するしきい値で判定する
    if (m_info.duration <= 0.0) return false;
    const double effectiveIn  = m_inSet  ? m_inSec  : 0.0;
    const double effectiveOut = m_outSet ? m_outSec : m_info.duration;
    constexpr double eps = 0.001;
    return effectiveIn > eps || effectiveOut < m_info.duration - eps;
}

void MainWindow::setRunning(Operation op)
{
    m_runningOp = op;
    const bool running = (op != Operation::None);
    if (running) m_videoView->pause();

    setUiEnabled(!running);
    // 実行中は D&D を拒否する（ウィンドウ全体・プレビュー領域の両方）
    setAcceptDrops(!running);
    m_videoView->setAcceptDrops(!running);
    // 実行中はプレビュー領域のマウスクリックでの再生トグルも封じる
    m_videoView->setInteractive(!running);

    // 操作中のラベル切替
    // 変換はメニュー項目のテキストを、トリムはメインボタンとメニュー項目の双方を切り替える
    if (m_actConvert) {
        m_actConvert->setText(op == Operation::Convert ? "変換を中止する" : "ファイルを変換する");
    }
    if (m_actTrim) {
        m_actTrim->setText(op == Operation::Trim ? "トリムを中止する" : "ファイルをトリムする");
    }
    m_trimBtn->setText(op == Operation::Trim ? "中止" : "トリム");
    if (op == Operation::Trim) m_trimBtn->setEnabled(true);

    updateMenuActionEnabled();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // WM_SIZING を捕まえて RECT を直接書き換え、ドラッグ中もアスペクト比を維持する
    // 事後補正方式と異なりマウスドラッグの毎フレームに反映されるため、
    // リリース時のスナップバック（ドラッグ中サイズと最終サイズの食い違い）が起きない
    if (eventType != "windows_generic_MSG") {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    MSG* msg = static_cast<MSG*>(message);

    // ドラッグ中も Qt のイベントループを駆動させて再生を継続させる
    // modal size/move ループ中は通常の Qt ディスパッチが止まるため QMediaPlayer の
    // キューシグナル経由のフレームが滞留する。Win32 タイマは modal loop 内でも
    // 発火するため、これをフックして processEvents() でキューを drain する
    // Qt 内部の USER タイマ（1 付近から昇順割り当て）との衝突を避けた任意の固定 ID
    static constexpr UINT kSizeMoveTimerId = 0xAB1E;
    // 最小値指定で WM_TIMER の最小解像度（USER_TIMER_MINIMUM = 10ms 程度）に丸まる
    static constexpr UINT kSizeMoveTimerInterval = 1;
    if (msg->message == WM_ENTERSIZEMOVE) {
        if (SetTimer(msg->hwnd, kSizeMoveTimerId, kSizeMoveTimerInterval, nullptr)) {
            m_sizeMoveTimerActive = true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    if (msg->message == WM_EXITSIZEMOVE) {
        if (m_sizeMoveTimerActive) {
            KillTimer(msg->hwnd, kSizeMoveTimerId);
            m_sizeMoveTimerActive = false;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    if (msg->message == WM_TIMER && msg->wParam == static_cast<WPARAM>(kSizeMoveTimerId)) {
        // QQuickView render thread との sync ステップを GUI スレッド側で処理する
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    if (msg->message != WM_SIZING) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    // 動画モード以外（音声・未読込）はアスペクト維持の対象外
    // 音声モードは setFixedHeight により Qt 側で縦が固定されているため別途介在不要
    // ガード節は QMainWindow::nativeEvent への委譲で統一し、*result の伝搬経路を一本化する
    if (isAudioOnly() || !m_info.valid || m_lowerUiH <= 0) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    if (windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }
    if (m_videoAspect <= 0.0) {
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    RECT* r = reinterpret_cast<RECT*>(msg->lParam);
    const WPARAM edge = msg->wParam;

    // ウィンドウフレーム余白を frameGeometry() と geometry() の差から取得する
    // RECT はフレーム外周の矩形のため、クライアント領域に変換するためにフレームを差し引く
    const QRect fg = frameGeometry();
    const QRect cg = geometry();
    const int frameLeft   = cg.left()   - fg.left();
    const int frameTop    = cg.top()    - fg.top();
    const int frameRight  = fg.right()  - cg.right();
    const int frameBottom = fg.bottom() - cg.bottom();

    int clientW = (r->right - r->left) - frameLeft - frameRight;
    int clientH = (r->bottom - r->top) - frameTop - frameBottom;

    // 上下辺ドラッグは高さマスター、それ以外（左右辺・角）は幅マスターとして扱う
    const bool heightMaster = (edge == WMSZ_TOP || edge == WMSZ_BOTTOM);

    if (heightMaster) {
        const int previewH = clientH - m_lowerUiH;
        if (previewH <= 0) return QMainWindow::nativeEvent(eventType, message, result);
        clientW = qRound(previewH * m_videoAspect);
    }
    else {
        const int previewH = qRound(clientW / m_videoAspect);
        clientH = previewH + m_lowerUiH;
    }

    // 最小サイズへのクランプ
    // WM_GETMINMAXINFO 経由の Qt 最小サイズ制約は WM_SIZING の RECT 書き換え後にも適用されるが、
    // クランプによりアスペクト比が崩れるためこちら側で先に整合させる
    if (clientW < kInitialWindowW) {
        clientW = kInitialWindowW;
        clientH = qRound(clientW / m_videoAspect) + m_lowerUiH;
    }
    if (clientH < kInitialWindowH) {
        clientH = kInitialWindowH;
        const int previewH = clientH - m_lowerUiH;
        if (previewH > 0) clientW = qRound(previewH * m_videoAspect);
    }

    const int newW = clientW + frameLeft + frameRight;
    const int newH = clientH + frameTop + frameBottom;

    // 辺・角ごとに RECT のどの座標を動かすか決定する
    // ドラッグした辺の対辺をアンカーすることで OS のドラッグ感覚と一致させる
    switch (edge) {
    case WMSZ_LEFT:
        r->left   = r->right  - newW;
        r->bottom = r->top    + newH;
        break;
    case WMSZ_RIGHT:
        r->right  = r->left   + newW;
        r->bottom = r->top    + newH;
        break;
    case WMSZ_TOP:
        r->top    = r->bottom - newH;
        r->right  = r->left   + newW;
        break;
    case WMSZ_BOTTOM:
        r->bottom = r->top    + newH;
        r->right  = r->left   + newW;
        break;
    case WMSZ_TOPLEFT:
        r->top    = r->bottom - newH;
        r->left   = r->right  - newW;
        break;
    case WMSZ_TOPRIGHT:
        r->top    = r->bottom - newH;
        r->right  = r->left   + newW;
        break;
    case WMSZ_BOTTOMLEFT:
        r->bottom = r->top    + newH;
        r->left   = r->right  - newW;
        break;
    case WMSZ_BOTTOMRIGHT:
        r->bottom = r->top    + newH;
        r->right  = r->left   + newW;
        break;
    default:
        return false;
    }

    *result = TRUE;
    return true;
}

void MainWindow::updateRangeMarkers()
{
    // 区間が変わったら過去の進捗オーバーレイは無効になる
    m_seekSlider->clearProgress();

    // 区間変化に追従してトリムボタンの活性状態を更新する
    // 実行中はこの直後に setRunning() → setUiEnabled(false) で再度無効化される
    const bool ffmpegOk = !m_ffmpegPath.isEmpty() && QFile::exists(m_ffmpegPath);
    m_trimBtn->setEnabled(m_info.valid && ffmpegOk && isTrimMeaningful());

    // 区間更新でメニュー側の「トリム」項目の活性条件も変わる
    updateMenuActionEnabled();

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
        // 実行中（変換またはトリム）はシーク・再生トグル・速度変更を全て無効化する
        // （Space / ←→ / ↑↓ いずれも処理負荷の増加と誤操作要因になる）
        if (m_runningOp != Operation::None) return true;

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
        case Qt::Key_R:
            // 区間マーカーのみクリア（再生位置・再生状態は維持する）
            // onStop は再生位置を 0 に戻すため別実装
            if (m_info.valid) {
                m_inSet  = false;
                m_outSet = false;
                m_inSec  = 0.0;
                m_outSec = m_info.duration;
                updateRangeMarkers();
            }
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

QString MainWindow::waveformCachePath(const QString& inputPath) const
{
    // 入力パスと mtime を組み合わせてハッシュ化する
    // 同一パスでもファイル更新を検出して再生成する仕組み
    const QFileInfo fi(inputPath);
    // フィルタ識別子（"|cbrt"）を含めることでフィルタ仕様変更時に自動的に新キャッシュへ移行する
    // 旧キャッシュは別ハッシュ名のまま %TEMP% に残存、OS の一時掃除に委ねる
    const QString key = fi.absoluteFilePath()
        + "@" + QString::number(fi.lastModified().toMSecsSinceEpoch())
        + "|cbrt";
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    // QCryptographicHash で決定論的なハッシュ値を得る
    // qHash はプロセスごとにシードがランダム化されるため、再起動で衝突せずキャッシュが機能しなくなる
    const QByteArray digest = QCryptographicHash::hash(
        key.toUtf8(), QCryptographicHash::Md5).toHex();
    return tmpDir + "/avply_wave_" + QString::fromLatin1(digest) + ".png";
}

void MainWindow::startWaveformGeneration(const QString& inputPath)
{
    // 短時間でファイルを切り替えた際に古いプロセスのコールバックが新ファイルへ
    // 誤反映するのを防ぐため、新規生成前に旧プロセスを停止する
    stopWaveformProcess(false);

    // ffmpeg パスが無効なら波形生成は諦める。シークバーは波形なしのまま
    if (m_ffmpegPath.isEmpty() || !QFile::exists(m_ffmpegPath)) return;

    const QString cachePath = waveformCachePath(inputPath);

    // キャッシュヒット時は ffmpeg を起動せず即時反映する
    if (QFile::exists(cachePath)) {
        const QPixmap pix(cachePath);
        if (!pix.isNull()) {
            m_seekSlider->setWaveform(pix);
            return;
        }
    }

    m_waveformProcOutPath = cachePath;
    m_waveformProc = Ffmpeg::generateWaveform(
        m_ffmpegPath, inputPath, cachePath,
        QSize(kWaveformW, kWaveformH), this,
        [this, cachePath](bool ok, const QString& /*outputPath*/) {
        m_waveformProc = nullptr;
        m_waveformProcOutPath.clear();
        if (!ok) {
            // 生成失敗は無音動画やデコードエラー等。中央基線にフォールバックする
            m_seekSlider->setBaseline(true);
            return;
        }
        const QPixmap pix(cachePath);
        if (pix.isNull()) {
            m_seekSlider->setBaseline(true);
            return;
        }
        m_seekSlider->setWaveform(pix);
    });
}

void MainWindow::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    menu.addAction(m_actOpen);
    menu.addSeparator();
    menu.addAction(m_actConvert);
    menu.addAction(m_actTrim);
    menu.addSeparator();

    QMenu* settings = menu.addMenu("設定");
    settings->addAction(m_actTopmost);
    settings->addAction(m_actSingleInst);
    settings->addAction(m_actPriority);

    // tooltip をメニュー項目に表示するため明示有効化する
    settings->setToolTipsVisible(true);

    menu.exec(event->globalPos());
}

void MainWindow::onToggleTopmost(bool checked)
{
    Settings::instance().setTopmostWhilePlaying(checked);
    applyTopmostState();
}

void MainWindow::onToggleSingleInstance(bool checked)
{
    // 単一インスタンス強制：トグル時の即時反映はせず、レジストリ保存のみ
    // 既存ウィンドウの IPC サーバ起動状態を変えると状態遷移が複雑化するため、次回起動から有効とする
    Settings::instance().setSingleInstance(checked);
}

void MainWindow::onTogglePriority(bool checked)
{
    // プロセス優先度はトグルと同時に即時反映する
    Settings::instance().setAboveNormalPriority(checked);
    SetPriorityClass(GetCurrentProcess(),
                     checked ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}

void MainWindow::applyTopmostState()
{
    // ウィンドウの最前面フラグの切り替え
    // QWindow::setFlag は Qt の windowFlags 状態と Win32 の WS_EX_TOPMOST を同時に更新し、
    // QWidget::setWindowFlag のような再 show を伴わない。Qt の内部状態と Win32 拡張スタイル
    // の不一致を防ぐため SetWindowPos を直接呼ぶより安定する
    const bool wantTopmost = Settings::instance().topmostWhilePlaying() && m_isPlaying;

    QWindow* w = windowHandle();
    if (w) {
        w->setFlag(Qt::WindowStaysOnTopHint, wantTopmost);
    }

    // フラグ更新後に Z オーダーを即時反映する
    // setFlag() は WS_EX_TOPMOST の付与・解除のみで、HWND_TOPMOST 順序へのソート
    // （他アプリより上に持ち上げる）は SetWindowPos を併用する必要がある
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd,
                 wantTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainWindow::loadFileFromIpc(const QString& path)
{
    // 別インスタンスから引数を受け取った時の取り込み口
    // 現在処理中（変換／トリム）なら割り込まずに前面化のみ行う
    if (!path.isEmpty() && m_runningOp == Operation::None && isAcceptedMedia(path) && QFile::exists(path)) {
        loadFile(path);
    }

    // ウィンドウを最前面に持ち上げてユーザに通知する
    // 最小化されている場合は復元してから activate
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
}

void MainWindow::stopWaveformProcess(bool synchronous)
{
    if (!m_waveformProc) return;

    // disconnect でコールバック経路を切ってから kill する
    // 同スレッド DirectConnection 想定だが、disconnect により受信側に二度と届かないことを保証する
    disconnect(m_waveformProc, nullptr, this, nullptr);
    m_waveformProc->kill();

    if (synchronous) {
        m_waveformProc->waitForFinished(3000);
        delete m_waveformProc;
    }
    else {
        m_waveformProc->deleteLater();
    }
    m_waveformProc = nullptr;

    // 中途まで書かれた可能性のある PNG を削除する
    // 次回起動時に QFile::exists ヒット → QPixmap が破損ファイルを読み込む事故を防ぐ
    if (!m_waveformProcOutPath.isEmpty()) {
        QFile::remove(m_waveformProcOutPath);
        m_waveformProcOutPath.clear();
    }
}
