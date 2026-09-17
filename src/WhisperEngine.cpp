#include "WhisperEngine.h"
#include <QFile>
#include <QThread>
#include <QStringList>
#include <QDebug>
#include <cstring>
#include <vector>

#include <whisper.h>
#include <ggml.h>
#include <ggml-backend.h>

namespace {

// whisper / ggml のログを avply.log へ転送するコールバック
// GUI アプリは stderr を持たないため、ライブラリ既定の出力先では診断できない。
// INFO 以下はモデル構成やテンソル配置の詳細で量が多く、常用の診断に要らないため落とす
void logToQt(ggml_log_level level, const char* text, void* /*user*/)
{
    if (level != GGML_LOG_LEVEL_WARN && level != GGML_LOG_LEVEL_ERROR) return;
    qWarning().noquote() << "whisper:" << QString::fromUtf8(text).trimmed();
}

// ggml のバックエンド DLL を 1 回だけ読み込み、結果をログへ残す
// ggml_backend_load_all() は exe と同階層の ggml-*.dll を走査する。ggml-vulkan.dll があっても
// Vulkan 対応ドライバが無ければロードに失敗して無視され、CPU バックエンドだけが残る。
// どのバックエンドが有効かは GPU 利用の可否を切り分ける唯一の手掛かりのため、必ず記録する
void loadBackendsOnce()
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    ggml_backend_load_all();

    QStringList names;
    const size_t count = ggml_backend_reg_count();
    for (size_t i = 0; i < count; ++i) {
        names << QString::fromUtf8(ggml_backend_reg_name(ggml_backend_reg_get(i)));
    }
    qWarning().noquote() << "WhisperEngine: backends=" << names.join(',');
}

} // namespace

WhisperEngine::WhisperEngine(QObject* parent)
    : QObject(parent)
{
    // ログ経路の差し替えはプロセス全体に効くため、バックエンドのロードより前に済ませる
    whisper_log_set(logToQt, nullptr);
    ggml_log_set(logToQt, nullptr);
}

WhisperEngine::~WhisperEngine()
{
    releaseModel();
}

void WhisperEngine::releaseModel()
{
    if (!m_ctx) return;
    whisper_free(m_ctx);
    m_ctx = nullptr;
    m_loadedModelPath.clear();
}

bool WhisperEngine::ensureModel(const QString& modelPath)
{
    if (m_ctx && m_loadedModelPath == modelPath) return true;

    releaseModel();
    loadBackendsOnce();

    whisper_context_params cparams = whisper_context_default_params();
    // 利用可能な GPU バックエンドがあれば使う。無ければ whisper が CPU へ落とす
    cparams.use_gpu = true;

    m_ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);
    if (!m_ctx) {
        qWarning() << "WhisperEngine: モデルのロードに失敗:" << modelPath;
        emit modelLoadFailed(modelPath);
        return false;
    }
    m_loadedModelPath = modelPath;
    return true;
}

void WhisperEngine::transcribe(quint64 jobId, const QString& modelPath,
                               const QString& pcmPath, const QString& language,
                               const QString& prompt)
{
    m_jobId = jobId;
    // キューで待つ間に取り消されたジョブは、モデルのロードにも入らず終える
    if (jobId <= m_cancelBelow.load(std::memory_order_relaxed)) {
        emit finished(jobId, false);
        return;
    }

    if (!ensureModel(modelPath)) {
        emit finished(jobId, false);
        return;
    }

    // PCM を丸ごと読んで float 列へ移す
    // 呼び出し側が停止時に削除できるよう、読み終えた時点でファイルを閉じる（QFile のスコープ）
    std::vector<float> samples;
    {
        QFile f(pcmPath);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning() << "WhisperEngine: PCM を開けない:" << pcmPath;
            emit finished(jobId, false);
            return;
        }
        const QByteArray raw = f.readAll();
        if (raw.isEmpty() || (raw.size() % static_cast<qsizetype>(sizeof(float))) != 0) {
            qWarning() << "WhisperEngine: PCM のサイズが float 列として不正:" << raw.size();
            emit finished(jobId, false);
            return;
        }
        samples.resize(static_cast<size_t>(raw.size()) / sizeof(float));
        std::memcpy(samples.data(), raw.constData(), static_cast<size_t>(raw.size()));
    }

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    // 認識は本スレッドを占有するが、GUI thread と audio thread を圧迫しないよう 1 本残す
    params.n_threads = qMax(1, QThread::idealThreadCount() - 1);
    // ライブラリ側の標準出力への表示はすべて止め、結果はコールバックだけで受け取る
    params.print_special    = false;
    params.print_progress   = false;
    params.print_realtime   = false;
    params.print_timestamps = false;
    params.translate        = false;
    params.no_timestamps    = false;

    // language と initial_prompt は params が const char* で保持し、whisper_full が実行中ずっと
    // 参照する。いずれも QByteArray をこのスコープで保持して寿命を合わせる
    const QByteArray lang = language.toUtf8();
    params.language = lang.constData();

    // 事前文脈を与えて固有名詞や専門用語へ認識を寄せる
    // carry_initial_prompt を立てるのは、既定の false では 30 秒のデコード窓ごとに
    // 直前の認識結果が文脈を埋め、長い録画の後半で用語のヒントが消えるためだ。
    // 代償として直前のテキストへの条件付けは弱まるが、会議録では用語の一貫性を優先する
    const QByteArray promptUtf8 = prompt.toUtf8();
    if (!promptUtf8.isEmpty()) {
        params.initial_prompt       = promptUtf8.constData();
        params.carry_initial_prompt = true;
    }

    params.new_segment_callback           = &WhisperEngine::newSegmentCb;
    params.new_segment_callback_user_data = this;
    params.progress_callback              = &WhisperEngine::progressCb;
    params.progress_callback_user_data    = this;
    params.abort_callback                 = &WhisperEngine::abortCb;
    params.abort_callback_user_data       = this;

    const int rc = whisper_full(m_ctx, params, samples.data(), static_cast<int>(samples.size()));
    const bool cancelled = abortCb(this);
    if (rc != 0 && !cancelled) {
        qWarning() << "WhisperEngine: 認識に失敗 rc=" << rc;
    }
    emit finished(jobId, rc == 0 && !cancelled);
}

void WhisperEngine::newSegmentCb(whisper_context* ctx, whisper_state* /*state*/,
                                 int nNew, void* user)
{
    auto* self = static_cast<WhisperEngine*>(user);
    const int n = whisper_full_n_segments(ctx);
    for (int i = n - nNew; i < n; ++i) {
        const QString text = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).trimmed();
        if (text.isEmpty()) continue;
        SubtitleCue cue;
        // whisper の区間時刻はセンチ秒。SubtitleCue はミリ秒
        cue.startMs = whisper_full_get_segment_t0(ctx, i) * 10;
        cue.endMs   = whisper_full_get_segment_t1(ctx, i) * 10;
        cue.text    = text;
        emit self->cueAdded(self->m_jobId, cue);
    }
}

void WhisperEngine::progressCb(whisper_context* /*ctx*/, whisper_state* /*state*/,
                               int progress, void* user)
{
    auto* self = static_cast<WhisperEngine*>(user);
    // 100% は finished の受信で確定させるため、進捗の上限は 99 に抑える
    emit self->progressChanged(self->m_jobId, qBound(0, progress, 99));
}

bool WhisperEngine::abortCb(void* user)
{
    auto* self = static_cast<WhisperEngine*>(user);
    return self->m_jobId <= self->m_cancelBelow.load(std::memory_order_relaxed);
}
