#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
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
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpenFile();
    void onSeekSliderChanged(int value);
    void onPlayerPositionChanged(qint64 ms);
    void onSetIn();
    void onSetOut();
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

    // アプリケーション全体のキー入力を捕捉して左右カーソルシークに変換する
    bool eventFilter(QObject* watched, QEvent* event) override;

    // 動画情報
    QString   m_filePath;
    VideoInfo m_info;
    double    m_inSec  = 0.0;
    double    m_outSec = 0.0;
    bool      m_inSet  = false;
    bool      m_outSet = false;

    // 設定
    QString m_ffmpegPath;
    int     m_seekLeftMs  = 3000;
    int     m_seekRightMs = 3000;

    // ウィジェット
    QLabel*       m_filePathLabel;
    QPushButton*  m_openBtn;
    VideoView*    m_videoView;
    QLabel*       m_posLabel;
    RangeSlider*  m_seekSlider;
    QPushButton*  m_setInBtn;
    QLabel*       m_inLabel;
    QPushButton*  m_setOutBtn;
    QLabel*       m_outLabel;
    QProgressBar* m_progressBar;
    QPushButton*  m_convertBtn;
    QLabel*       m_outputLabel;

    Encoder* m_encoder = nullptr;

    // シーク要求のスロットル（連続 valueChanged を間引く）
    QTimer  m_seekTimer;
    qint64  m_pendingSeekMs = -1;
};
