#pragma once
#include <QObject>
#include <QString>
#include "SubtitleTrack.h"

class QProcess;
class QThread;
class WhisperEngine;

// 字幕生成の実行とキャッシュを担う
// 1 メディアにつき ffmpeg（16kHz モノラル float32 PCM 抽出）→ 組み込み whisper.cpp
// （WhisperEngine、専用スレッド）の 2 段を順に走らせる。区間が確定するたび cueAdded で
// 逐次通知し、認識の進捗は progressChanged で通知する。
// 完了時は全キューを SRT としてキャッシュへ保存する（キューが 1 件も無ければ作らない）。
// 次回以降は ffmpeg も認識も走らせず、キャッシュから即時に全キューを通知する。
//
// キャッシュのキーはメディアの部分ハッシュ（mediaHash）と認識条件（モデル名・言語・prompt）の
// 組で、パス・更新日時に依存しない。同じ動画をコピーしても移動しても再認識しない。
// 認識条件を含めるのは、モデルや prompt を変えたのに前の認識結果が出続けるのを防ぐためだ。
//
// 中間 PCM は %TEMP% 直下にハッシュ名で置く。認識は全サンプルをメモリへ載せてから走るため、
// 起動直後にファイルを閉じる。エンジンがファイルを掴み続けて停止時の削除を妨げることはない。
//
// 失敗（ffmpeg の異常終了、モデルのロード失敗、PCM の読み込み失敗）は avply.log へ警告を残し、
// finished(false) で呼び出し側へ通知する。それまでに通知したキューは有効なまま残り、
// 呼び出し側が破棄しない限り表示に使える。再試行はしない。
class SubtitleTranscriber : public QObject {
    Q_OBJECT
public:
    struct Params {
        QString ffmpegPath;   // 音声抽出に使う ffmpeg.exe
        QString modelPath;    // ggml モデル（.bin）
        QString language;     // whisper へ渡す言語コード（例：ja）
        QString prompt;       // whisper へ渡す事前文脈（空なら渡さない）
        QString cacheDir;     // SRT キャッシュの置き場（無ければ作る）
    };

    explicit SubtitleTranscriber(QObject* parent = nullptr);
    ~SubtitleTranscriber() override;

    // mediaPath の字幕生成を開始する
    // 実行中なら stop() で打ち切ってから始める。キャッシュ命中時はプロセスも認識も起動せず、
    // この呼び出しの中で全キューの cueAdded と finished(true) を同期的に emit する
    void start(const Params& params, const QString& mediaPath);

    // 実行中の抽出と認識を止め、中間 PCM を削除する
    // 以後 cueAdded / progressChanged / finished は発火しない。実行中でなければ何もしない
    void stop();

    // ロード済みモデルを解放してメモリを返す（字幕 OFF 時に呼ぶ）
    void releaseModel();

    // メディアの部分ハッシュ（16 進小文字）を返す
    // ファイルサイズ + 先頭 1MiB + 末尾 1MiB の SHA-256。全体を読まないため数 GB でも一瞬で済む。
    // 同一サイズで中身だけ違う動画は実用上存在しないため、これで同一性判定とする。
    // 読み出しに失敗したら空文字を返す
    static QString mediaHash(const QString& path);

    // 認識条件（モデル名・言語・prompt）の短いハッシュを返す
    // SRT キャッシュのファイル名へ mediaHash と並べて埋め、条件を変えた認識を別物として扱う。
    // モデルはフルパスではなくファイル名で見る。アプリを別の場所へ移してもキャッシュを保つためだ。
    // 代償として、別ディレクトリに置いた同名の別モデルは区別しない。設定でモデルをパス指定して
    // 同名のまま実体を入れ替えた場合は、キャッシュを手で消さないと前の認識結果が出続ける
    static QString recognitionKey(const Params& params);

signals:
    // 字幕キューが 1 件確定したとき発火する（認識の逐次出力、またはキャッシュ復元）
    void cueAdded(const SubtitleCue& cue);

    // 認識の進捗が変化したとき発火する（0〜99）
    void progressChanged(int percent);

    // 生成が終了したとき発火する。ok=false は上記の失敗のいずれかを示す
    void finished(bool ok);

    // モデルのロードに失敗したとき、finished より先に発火する
    // 受け手はモデルが壊れている前提で扱う（詳細は WhisperEngine の同名シグナル）
    void modelLoadFailed(const QString& modelPath);

private slots:
    // WhisperEngine からの通知。自分が最後に発行したジョブ以外は捨てる
    void onEngineCue(quint64 jobId, const SubtitleCue& cue);
    void onEngineProgress(quint64 jobId, int percent);
    void onEngineFinished(quint64 jobId, bool ok);

private:
    // ffmpeg で 16kHz モノラル float32 PCM を抽出する。完了後 startRecognize へ進む
    void startExtract();

    // 抽出済み PCM の認識を認識スレッドへ依頼する
    void startRecognize();

    // 実行中プロセスを解放する（disconnect → kill → 短時間 wait → deleteLater）
    // ~QProcess() の waitForFinished(30000) ブロックを避けるため親から切り離す
    void releaseProcess();

    // 生成の終了処理。中間 PCM を削除する
    // 成功かつキューが 1 件以上のときだけキャッシュへ SRT を書く
    // 無音メディアは成功してもキャッシュを作らず、次回も再認識する（空 SRT を残さないため）
    void finish(bool ok);

    Params        m_params;
    QString       m_mediaPath;
    QString       m_pcmPath;
    QString       m_cachePath;
    QProcess*     m_proc = nullptr;
    SubtitleTrack m_track;

    // 認識エンジンとその専用スレッド
    QThread*       m_engineThread = nullptr;
    WhisperEngine* m_engine       = nullptr;

    // 発行済みジョブの識別子。stop / 新規 start のたびに進め、旧ジョブの遅延通知を捨てる
    quint64        m_jobId = 0;
};
