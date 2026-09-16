#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include "SubtitleTrack.h"

struct whisper_context;
struct whisper_state;

// whisper.cpp による音声認識エンジン
// 専用スレッドへ moveToThread して使う（AudioWorker と同じ構成）。認識は whisper_full の
// 単一呼び出しで完結し、区間が確定するたびコールバックが cueAdded を emit する。
//
// モデルはロードしたまま保持し、同じパスの次の認識で再利用する。ロードは数秒かかるため、
// ファイルを切り替えるたびの再ロードを避ける狙いだ。解放は releaseModel() の明示呼び出しに限る
// （モデルは数百 MB〜1GB を占めるため、字幕 OFF で返す）。
//
// ggml のバックエンドは初回ロード時に ggml_backend_load_all() が exe と同階層の ggml-*.dll から
// 読み込む。ggml-vulkan.dll があり Vulkan 対応ドライバがあれば GPU、無ければ CPU で動く。
// モデルパスは UTF-8 で渡してよい（whisper.cpp が MSVC 向けに UTF-16 へ変換して開く）
class WhisperEngine : public QObject {
    Q_OBJECT
public:
    explicit WhisperEngine(QObject* parent = nullptr);
    ~WhisperEngine() override;

    // jobId 以下のジョブを取り消す
    // 任意のスレッドから呼べる（アトミックな基準値を更新するだけ）。実行中のジョブは abort
    // コールバック経由で即座に止まり、キュー待ちのジョブは開始せずに終わる。
    // 認識は数分かかるため、停止要求が実行中のジョブにしか効かないと、待ち行列の
    // 古いジョブが後から丸ごと走ってしまう。両方を 1 つの基準値で止める
    void cancelUpTo(quint64 jobId) { m_cancelBelow.store(jobId, std::memory_order_relaxed); }

public slots:
    // pcmPath（16kHz モノラル float32 の生 PCM）を認識する
    // jobId は結果の識別子で、呼び出し側は自分が最後に発行した id 以外の通知を捨てる。
    // モデルが未ロード、または前回と違うパスならロードし直す
    void transcribe(quint64 jobId, const QString& modelPath, const QString& pcmPath,
                    const QString& language);

    // ロード済みモデルを解放してメモリを返す（未ロードなら何もしない）
    void releaseModel();

signals:
    // 区間が 1 件確定したとき発火する
    void cueAdded(quint64 jobId, const SubtitleCue& cue);

    // 認識の進捗が変化したとき発火する（0〜99。100 は finished が確定する）
    void progressChanged(quint64 jobId, int percent);

    // 認識が終了したとき発火する。ok=false はモデルのロード失敗・PCM 読み込み失敗・取り消し
    void finished(quint64 jobId, bool ok);

    // モデルのロードに失敗したとき、finished より先に発火する
    // ファイルの削除や再取得はエンジンの責務ではないため、判断を呼び出し側へ委ねる。
    // 中身が壊れたモデルを放置すると、以後は毎回同じ失敗を繰り返して字幕が使えなくなる
    void modelLoadFailed(const QString& modelPath);

private:
    // モデルをロードする（同じパスがロード済みなら何もしない）
    // 初回呼び出しでバックエンド DLL の読み込みと、読み込めたバックエンド名のログ出力も行う
    bool ensureModel(const QString& modelPath);

    // whisper へ渡すコールバック群（user_data は this）
    // whisper_full の呼び出しスレッド（本オブジェクトの所属スレッド）から同期実行される
    static void newSegmentCb(whisper_context* ctx, whisper_state* state, int nNew, void* user);
    static void progressCb(whisper_context* ctx, whisper_state* state, int progress, void* user);
    static bool abortCb(void* user);

    whisper_context* m_ctx = nullptr;
    QString          m_loadedModelPath;
    // 取り消しの基準値。この値以下の jobId は実行しない（cancelUpTo のコメント参照）
    std::atomic<quint64> m_cancelBelow{0};
    // 実行中ジョブの識別子。コールバックが emit に添える
    quint64          m_jobId = 0;
};
