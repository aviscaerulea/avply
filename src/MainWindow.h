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

// アプリケーションのメインウィンドウ
// ファイル選択・シーク・開始/終了設定・変換実行を担う
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // initialPath にパスを渡すと起動完了後にそのファイルを読み込む
    // （Windows の D&D 起動・「送る」・「プログラムを指定して開く」想定）
    explicit MainWindow(const QString& initialPath = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // ウィンドウリサイズ時に動画アスペクト比に合わせて高さを矯正する
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onOpenFile();
    void onSeekSliderChanged(int value);
    void onPlayerPositionChanged(qint64 ms);
    void onSetIn();
    void onSetOut();
    void onStop();
    void onConvertOrCancel();
    void onEncoderProgress(int pct);
    void onEncoderFinished(bool ok, const QString& outputPath, const QString& err);

private:
    // 動画ファイルを実際に読み込む（Open ダイアログと D&D 共通）
    void loadFile(const QString& path);

    // 拡張子が動画として受け付け可能か判定する
    static bool isAcceptedVideo(const QString& path);

    // UI 有効/無効を切り替える（変換中は無効化）
    void setUiEnabled(bool enabled);

    // 変換中表示に切り替える
    void setConverting(bool converting);

    // 区間マーカーをスライダーに反映する
    void updateRangeMarkers();

    // スライダー値を秒数に変換する
    double sliderToSec(int value) const;

    // 秒数を HH:MM:SS 形式の文字列に変換する
    static QString formatSec(double sec);

    // ffmpeg パスの妥当性を検査し、不正なら警告ダイアログを 1 回出す
    void validateFfmpegPath();

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

    // ウィンドウ最小サイズを動画アスペクト比から再計算する
    void updateMinimumWindowSize();

    // 動画情報
    QString   m_filePath;
    VideoInfo m_info;
    double    m_inSec  = 0.0;
    double    m_outSec = 0.0;
    bool      m_inSet  = false;
    bool      m_outSet = false;

    // 設定
    QString m_ffmpegPath;
    int     m_seekLeftMs  = 5000;
    int     m_seekRightMs = 5000;

    // 動画読込時の初期ウィンドウサイズ上限のモニタ比率（avply.toml の [window].initial_screen_ratio）
    double m_initialScreenRatio = 0.8;

    // 現在の再生速度（1.0 = 等速）
    qreal m_playbackRate = 1.0;

    // ウィンドウのアスペクト比連動用状態
    // m_videoAspect は現在の基準比率（動画未読込時は 16:9 = 800/450）
    // m_lowerUiH はレイアウト確定後の下部 UI 合計高さ（一度だけ取得）
    // m_resizingProgrammatically は resizeEvent 内 resize の再帰防止フラグ
    double m_videoAspect            = 16.0 / 9.0;
    int    m_lowerUiH               = 0;
    bool   m_resizingProgrammatically = false;

    // ウィジェット
    QLabel*       m_filePathLabel;
    QPushButton*  m_openBtn;
    VideoView*    m_videoView;
    QPushButton*  m_playPauseBtn;
    QPushButton*  m_stopBtn;

    // 再生状態切替で頻繁に差し替えるアイコンはコンストラクタで一度だけ生成して保持する
    QIcon m_iconPlay;
    QIcon m_iconPause;
    QLabel*       m_posLabel;
    QLabel*       m_speedLabel;
    RangeSlider*  m_seekSlider;
    QPushButton*  m_setInBtn;
    QPushButton*  m_setOutBtn;
    QPushButton*  m_convertBtn;
    QLabel*       m_videoInfoLabel;
    QLabel*       m_outputLabel;

    Encoder* m_encoder = nullptr;

    // シーク要求のスロットル（連続 valueChanged を間引く）
    QTimer  m_seekTimer;
    qint64  m_pendingSeekMs = -1;
};
