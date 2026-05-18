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
#include "SeekPreview.h"
#include "ThumbnailExtractor.h"
#include "SilenceTone.h"

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
    void onCopyFilePath();
    void onEncoderProgress(int pct);
    void onEncoderFinished(bool ok, const QString& outputPath, const QString& err);

    // シークバーのホバー位置を受信して、サムネイル + 時刻のプレビューを表示する
    void onSeekHoverMoved(int x, int sliderValue);

    // マウスがシークバー外に出たときにプレビューを非表示にする
    void onSeekHoverLeft();

private:
    // メディアファイルを実際に読み込む（Open ダイアログと D&D 共通）
    // centerOnMonitor=true のときのみモニタ作業領域の中央へウィンドウを移動する
    void loadFile(const QString& path, bool centerOnMonitor = false);

    // ffprobe 完了後の UI 反映処理
    // loadFile の async コールバックから呼び出され、メディア情報のラベル更新・
    // ウィンドウサイズ調整・波形生成キックを担う
    void onProbeFinished(const QString& path, const VideoInfo& info, bool centerOnMonitor);

    // 拡張子がメディア（動画・音声）として受け付け可能か判定する
    static bool isAcceptedMedia(const QString& path);

    // 拡張子から音声ファイル（mp3/wav/flac/ogg/opus）かを判定する
    // ロード前の初期ウィンドウ構成に使う簡易判定。コンテナ内が実際に音声のみかは ffprobe で再判定する
    static bool isAudioByExtension(const QString& path);

    // ffprobe 結果が音声のみ（映像ストリーム未検出）であるかを返す
    // probe 未完了時は false（動画扱い）を返す。
    // VideoInfo メンバを probe 完了前に参照すると、設定済みの旧情報を流用してフリッカ要因になるため
    bool isAudioOnly() const { return m_info.valid && (m_info.codec.isEmpty() || m_info.width <= 0); }

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
    // disconnect → kill → 短時間 waitForFinished → setParent(nullptr) + deleteLater の順で
    // 解放することで、~QProcess() の waitForFinished(30000) ブロックを回避しつつ
    // ハンドルが解放されるのを待つ。kill した中途生成 PNG は QFile::remove で削除し
    // 次回起動時に破損キャッシュをヒットさせない
    void stopWaveformProcess();

    // カーソルキーによる相対シーク（delta > 0 で早送り、< 0 で巻き戻し）
    void seekRelative(int deltaMs);

    // 再生速度を相対変更してステータス表示を更新する（delta は 0.05 単位想定）
    void changePlaybackRate(qreal delta);

    // 再生速度ラベルの表示を現在値で更新する
    void updateSpeedDisplay();

    // 音量を相対変更してラベル表示と VideoView へ反映する（delta は 0.05 単位想定）
    void changeVolume(qreal delta);

    // ホイール入力を修飾子に応じてシーク／音量／再生速度に振り分ける
    // VideoView と RangeSlider の両 wheelScrolled 経路で共通使用する。
    // Ctrl 優先 → Shift → 修飾子なしシークの順。変換中はすべて抑止
    void handleWheelInput(bool forward, bool shift, bool ctrl);

    // 音量ラベルの表示を現在値で更新する
    void updateVolumeDisplay();

    // 「開く...」ダイアログの初期ディレクトリを返す
    // 動画読込済なら同フォルダ、未読込なら %USERPROFILE%
    QString openDialogStartDir() const;

    // アプリケーション全体のキー入力を捕捉してシーク・再生制御に変換する
    bool eventFilter(QObject* watched, QEvent* event) override;

    // メニューから操作する設定の即時反映用ハンドラ
    void onToggleTopmost(bool checked);
    void onToggleSingleInstance(bool checked);
    void onTogglePriority(bool checked);

    // ノーマライズの強度を 1 段階進める（Off → 小 → 中 → 大 → Off の 4 状態循環）
    // N キー押下から呼ばれる。レジストリ永続化と AudioWorker への反映、ラベル更新をまとめて行う
    void cycleNormalize();

    // 音声明瞭化の強度を 1 段階進める（Off → 小 → 中 → 大 → Off の 4 状態循環）
    // V キー押下から呼ばれる。レジストリ永続化と AudioWorker への反映、ラベル更新をまとめて行う
    void cycleVoiceClarity();

    // ノーマライズラベルの強度表記を現在の設定に応じて更新する
    // 常時表示で「Normalize:0/1/2/3」（0=Off / 1=小 / 2=中 / 3=大）の数値を表示する
    void updateNormalizeDisplay();

    // 音声明瞭化ラベルの強度表記を現在の設定に応じて更新する
    // 常時表示で「Clarity:0/1/2/3」（0=Off / 1=小 / 2=中 / 3=大）の数値を表示する
    void updateVoiceClarityDisplay();

    // g キー押下時のトグル動作
    // 1 回目で再生速度/音量/Normalize/Clarity を全て「中立値」へ揃え、
    // 2 回目で起動時に読み込んだ TOML / レジストリ値へ復元する
    void toggleGReset();

    // 再生速度・音量・Normalize・Clarity の 4 項目を一括適用する
    // toggleGReset 専用の内部ヘルパで、m_gResetActive フラグは操作しない（呼び出し側で管理）
    void applyPlaybackState(qreal rate, qreal vol, int normLevel, int clarityLevel);

    // 再生状態に応じてウィンドウの最前面表示を切り替える
    // Settings::topmostWhilePlaying が true かつ playing なら topmost、それ以外は解除
    void applyTopmostState();

    // メニューアクションの enabled 状態を現在の文脈に合わせて更新する
    void updateMenuActionEnabled();

    // 指定した画面座標にコンテキストメニューを表示する
    // contextMenuEvent と VideoView 経由のシグナルから共通で呼び出す
    void showContextMenuAt(const QPoint& globalPos);

    // 現在のシークスライダー位置から SeekPreview の表示位置を更新する
    void updateSeekPreviewPosition(int x);

    // 現在の m_hoverPendingSec を対象にサムネイル抽出を要求する
    // 完了 callback 内で最新ホバー位置が遷移していたら自分自身を再呼び出しして連鎖追従する
    void requestHoverThumbnail();

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
    double m_initialScreenRatio = 0.7;

    // 現在の再生速度（1.0 = 等速）
    qreal m_playbackRate = 1.0;

    // 現在の再生音量（0.0〜1.0）
    qreal m_volume = 1.0;

    // g キーで参照する起動時デフォルト値のスナップショット
    // TOML / レジストリから初回読込した値をコンストラクタで保存する
    qreal m_initialPlaybackRate      = 1.0;
    qreal m_initialVolume            = 1.0;
    int   m_initialNormalizeLevel    = 0;
    int   m_initialVoiceClarityLevel = 0;

    // g キーによる「全リセット状態」フラグ
    // true の間に手動で速度/音量/Normalize/Clarity のいずれかが変わると自動で false に戻り、
    // 次の g キー押下は再び「全リセット」として動作する
    bool  m_gResetActive = false;

    // ウィンドウのアスペクト比連動用状態
    // m_videoAspect は WM_SIZING 中に参照する動画の基準比率（動画未読込時は 16:9）
    // m_lowerUiH は下部 UI（seekRow + statusBar + 余白）の自然高合計
    double m_videoAspect            = 16.0 / 9.0;
    int    m_lowerUiH               = 0;

    // ウィジェット
    VideoView*    m_videoView;
    QPushButton*  m_playPauseBtn;
    QPushButton*  m_stopBtn;

    // 再生状態切替で頻繁に差し替えるアイコンはコンストラクタで一度だけ生成して保持する
    QIcon m_iconPlay;
    QIcon m_iconPause;
    QLabel*       m_posLabel;
    QLabel*       m_speedLabel;
    QLabel*       m_volumeLabel;
    QLabel*       m_normalizeLabel;
    QLabel*       m_voiceClarityLabel;
    RangeSlider*  m_seekSlider;
    QPushButton*  m_setInBtn;
    QPushButton*  m_setOutBtn;
    QPushButton*  m_trimBtn;
    QLabel*       m_videoInfoLabel;
    QLabel*       m_outputLabel;

    // コンテキストメニュー（右クリック）の各項目
    // 「変換」は実行中に「中止」表記へ切り替え、「トリム」はメインの m_trimBtn と同期する
    QAction*      m_actOpen          = nullptr;
    QAction*      m_actCopyPath      = nullptr;
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

    // 実行中の ffprobe プロセス。新規ファイル読込時に kill して旧結果を破棄する
    QProcess* m_probeProc = nullptr;

    // 現在生成中プロセスの出力先 PNG パス。kill 時に部分書き込みファイルを削除するため保持する
    QString m_waveformProcOutPath;

    // シークバーホバープレビュー
    SeekPreview*        m_seekPreview    = nullptr;
    ThumbnailExtractor* m_thumbExtractor = nullptr;

    // 最新の量子化済みホバー秒数（-1 = 未設定。シークバー外・ファイル未読込の初期状態）
    // -1 との比較により初回ホバーは必ず ffmpeg を起動する。
    // クロージャ内で「現在のホバー対象が同じ秒か」をチェックして古い結果の表示を防ぐ
    int    m_hoverPendingSec = -1;
    int    m_hoverLastX      = 0;

    // 実行中の操作種別。None ならアイドル
    Operation m_runningOp = Operation::None;

    // probe 失敗ダイアログ表示中の loadFile 再入抑止フラグ
    // QMessageBox::critical のネストイベントループ中に D&D 等で loadFile が呼ばれても無視する
    bool m_loadInhibited = false;

    // シーク要求のスロットル（連続 valueChanged を間引く）
    QTimer  m_seekTimer;
    qint64  m_pendingSeekMs = -1;

    // BT ヘッドセットのアイドル復帰時プチノイズ抑制用の常時不可聴トーン出力
    // QMediaPlayer とは独立した QAudioSink で 1 kHz / 約 -80 dBFS を流し続け、
    // 出力デバイスが省電力状態に落ちて再エンゲージするときの音切れを防ぐ
    SilenceTone* m_silenceTone = nullptr;
};
