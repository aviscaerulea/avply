#pragma once
#include <QObject>
#include <QString>
#include <QProcess>

// 変換処理モード
enum class EncodeMode {
    Reencode,    // av1_nvenc + libopus 再エンコード（高品質・低速）
    StreamCopy,  // -c copy ストリームコピー（高速・キーフレーム単位カット）
};

// ffmpeg に渡す変換パラメータ
struct EncodeParams {
    EncodeMode mode = EncodeMode::Reencode;
    QString inputPath;
    QString outputPath;
    double inSec = 0.0;        // カット開始位置（秒）
    double outSec = 0.0;       // カット終了位置（秒）
    int inputWidth = 0;        // 入力映像の幅（QWXGA 超判定に使用）
    bool hasVideo = true;      // 入力に映像ストリームを含むか（false なら音声のみ）
};

// ffmpeg による動画変換を管理するクラス
// 変換の開始・中断・進捗通知を担う
class Encoder : public QObject {
    Q_OBJECT
public:
    explicit Encoder(const QString& ffmpegPath, QObject* parent = nullptr);
    ~Encoder() override;

    // 変換を開始する
    void encode(const EncodeParams& params);

    // 変換を中断する
    void cancel();

    // 走行中の ffmpeg プロセスの終了待ち
    // 既に停止済みなら即時 true。タイムアウトすると false。デストラクタの終了応答性確保用
    bool waitForFinished(int timeoutMs);

    bool isRunning() const;

signals:
    // 進捗変化（0〜100）
    void progressChanged(int pct);

    // 変換完了。ok=false の場合は err にエラー内容が入る
    void finished(bool ok, const QString& outputPath, const QString& err);

private slots:
    void onReadyReadOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QString m_ffmpegPath;
    QProcess* m_process = nullptr;
    EncodeParams m_params;
    double m_totalDuration = 0.0; // out - in（秒）
    bool m_cancelled = false;     // ユーザによる cancel() 呼び出しフラグ
    QString m_tempPath;           // %TEMP% 内の一時出力パス
};
