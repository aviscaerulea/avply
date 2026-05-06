#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QTimer>
#include "FfmpegRunner.h"
#include "VideoView.h"
#include "RangeSlider.h"
#include "Encoder.h"

class QDragEnterEvent;
class QDropEvent;
class QProcess;
class QAction;
class QContextMenuEvent;

// アプリケーションのメインウィンドウ
// ファイル選択・シーク・開始/終了設定・変換実行を担う
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // initialPath にパスを渡すと起動完了後にそのファイルを読み込む
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」想定）
    explicit MainWindow(const QString& initialPath = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

    // IPC で他インスタンスから受信したファイルパスを取り込む
    // 受信時はウィンドウを前面化して可視性を確保する。空文字なら前面化のみ
    void loadFileFromIpc(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // 右クリックでコンテキストメニューを表示する
    void contextMenuEvent(QContextMenuEvent* event) override;

    // Windows ネイティブメッセージを処理する
    // WM_SIZING でウィンドウドラッグ中の RECT を直接書き換え、リアルタイムにアスペクト比を維持する
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void onOpenFile();
    void onSeekSliderChanged(int value);
    void onPlayerPositionChanged(qint64 ms);
    void onSetIn();
    void onSetOut();
    void onStop();
    void onConvertOrCancel();
    void onTrimOrCancel();
    void onEncoderProgress(int pct);
    void onEncoderFinished(bool ok, const QString& outputPath, const QString& err);

private:
    // メディアファイルを実際に読み込む（Open ダイアログと D&D 共通）
    void loadFile(const QString& path);

    // 拡張子がメディア（動画・音声）として受け付け可能か判定する
    static bool isAcceptedMedia(const QString& path);

    // 拡張子から音声ファイル（mp3/wav/flac/ogg/opus）かを判定する
    // ロード前の初期ウィンドウ構成に使う簡易判定。コンテナ内が実際に音声のみかは ffprobe で再判定する
    static bool isAudioByExtension(const QString& path);

    // ffprobe 結果が音声のみ（映像ストリーム未検出）であるかを返す
    bool isAudioOnly() const { return m_info.codec.isEmpty() || m_info.width <= 0; }

    // UI 有効/無効を切り替える（変換中は無効化）
    void setUiEnabled(bool enabled);

    // トリムが意味を持つか（実効範囲が動画全長と異なるか）を判定する
    bool isTrimMeaningful() const;

    // 実行中の操作種別。None ならアイドル
    enum class Operation { None, Convert, Trim };

    // 変換・トリム共通の起動/中止ハンドラ
    void startOrCancel(EncodeMode mode);

    // 実行状態に応じて UI をまとめて切り替える
    void setRunning(Operation op);

    // 区間マーカーをスライダーに反映する
    void updateRangeMarkers();

    // スライダー値を秒数に変換する
    double sliderToSec(int value) const;

    // 秒数を HH:MM:SS 形式の文字列に変換する
    static QString formatSec(double sec);

    // ffmpeg パスの妥当性を検査し、不正なら警告ダイアログを 1 回出す
    void validateFfmpegPath();

    // 音声波形 PNG の生成を非同期で起動する
    // キャッシュヒット時は ffmpeg を起動せず即時シークバーへ反映する
    void startWaveformGeneration(const QString& inputPath);

    // 入力ファイルパス + mtime をキーにした波形 PNG キャッシュパスを返す
    QString waveformCachePath(const QString& inputPath) const;

    // 実行中の波形生成プロセスを停止し m_waveformProc を解放する
    // synchronous=true で waitForFinished + delete（デストラクタ向け）、
    // false で deleteLater（ファイル切替時向け）。kill した中途生成 PNG は QFile::remove で削除し
    // 次回起動時に破損キャッシュをヒットさせない
    void stopWaveformProcess(bool synchronous);

    // カーソルキーによる相対シーク（delta > 0 で早送り、< 0 で巻き戻し）
    void seekRelative(int deltaMs);

    // 再生速度を相対変更してステータス表示を更新する（delta は 0.05 単位想定）
    void changePlaybackRate(qreal delta);

    // 再生速度ラベルの表示を現在値で更新する
    void updateSpeedDisplay();

    // 「開く...」ダイアログの初期ディレクトリを返す
    // 動画読込済なら同フォルダ、未読込なら %USERPROFILE%
    QString openDialogStartDir() const;

    // アプリケーション全体のキー入力を捕捉してシーク・再生制御に変換する
    bool eventFilter(QObject* watched, QEvent* event) override;

    // メニューから操作する設定の即時反映用ハンドラ
    void onToggleTopmost(bool checked);
    void onToggleSingleInstance(bool checked);
    void onTogglePriority(bool checked);

    // 再生状態に応じてウィンドウの最前面表示を切り替える
    // Settings::topmostWhilePlaying が true かつ playing なら topmost、それ以外は解除
    void applyTopmostState();

    // メニューアクションの enabled 状態を現在の文脈に合わせて更新する
    void updateMenuActionEnabled();

    // 動画情報
    QString   m_filePath;
    VideoInfo m_info;
    double    m_inSec  = 0.0;
    double    m_outSec = 0.0;
    bool      m_inSet  = false;
    bool      m_outSet = false;

    // 設定
    QString m_ffmpegPath;
    int     m_seekLeftMs       = 5000;
    int     m_seekRightMs      = 5000;
    int     m_seekWheelForwardMs = 5000;
    int     m_seekWheelBackMs    = 5000;

    // 動画読込時の初期ウィンドウサイズ上限のモニタ比率（avply.toml の [window].initial_screen_ratio）
    double m_initialScreenRatio = 0.8;

    // 現在の再生速度（1.0 = 等速）
    qreal m_playbackRate = 1.0;

    // ウィンドウのアスペクト比連動用状態
    // m_videoAspect は WM_SIZING 中に参照する動画の基準比率（動画未読込時は 16:9）
    // m_lowerUiH はレイアウト確定後の下部 UI 合計高さ（一度だけ取得）
    double m_videoAspect            = 16.0 / 9.0;
    int    m_lowerUiH               = 0;

    // WM_ENTERSIZEMOVE 突入時に再生中だったかを保持し、WM_EXITSIZEMOVE で再開判定する
    // Win32 の modal size/move ループ中は Qt のキューシグナル配送が止まり、
    // QMediaPlayer のフレーム滞留で再生が飛び飛びになる。ドラッグ中だけ pause して回避する
    bool m_resumeOnExitSizeMove = false;

    // ウィジェット
    QLabel*       m_filePathLabel;
    VideoView*    m_videoView;
    QPushButton*  m_playPauseBtn;
    QPushButton*  m_stopBtn;

    // 再生状態切替で頻繁に差し替えるアイコンはコンストラクタで一度だけ生成して保持する
    QIcon m_iconPlay;
    QIcon m_iconPause;
    QLabel*       m_posLabel;
    QLabel*       m_speedLabel;
    QLabel*       m_volumeLabel;
    RangeSlider*  m_seekSlider;
    QPushButton*  m_setInBtn;
    QPushButton*  m_setOutBtn;
    QPushButton*  m_trimBtn;
    QLabel*       m_videoInfoLabel;
    QLabel*       m_outputLabel;

    // コンテキストメニュー（右クリック）の各項目
    // 「変換」は実行中に「中止」表記へ切り替え、「トリム」はメインの m_trimBtn と同期する
    QAction*      m_actOpen          = nullptr;
    QAction*      m_actConvert       = nullptr;
    QAction*      m_actTrim          = nullptr;
    QAction*      m_actTopmost       = nullptr;
    QAction*      m_actSingleInst    = nullptr;
    QAction*      m_actPriority      = nullptr;

    // 現在の再生状態（applyTopmostState で参照）
    bool m_isPlaying = false;

    Encoder* m_encoder = nullptr;

    // 実行中の波形生成プロセス。新規ファイル読込時に kill して入れ替える
    QProcess* m_waveformProc = nullptr;

    // 現在生成中プロセスの出力先 PNG パス。kill 時に部分書き込みファイルを削除するため保持する
    QString m_waveformProcOutPath;

    // 実行中の操作種別。None ならアイドル
    Operation m_runningOp = Operation::None;

    // シーク要求のスロットル（連続 valueChanged を間引く）
    QTimer  m_seekTimer;
    qint64  m_pendingSeekMs = -1;
};
