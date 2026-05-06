#include "AudioWorker.h"
#include <cmath>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QDebug>

namespace {

// soft-clip 閾値
// 閾値以下は線形通過させ、閾値超過分のみ tanh で滑らかに飽和させる。
// 入力全体に tanh を適用すると小振幅サンプルでも非線形歪み（THD）が発生し、
// 「ザリザリ」感のあるノイズとして可聴化する。閾値以下を線形通過させることで
// 通常音量の信号品質を維持しつつ、overshoot 部分のみソフトクリップする。
// 0.8 ≈ -1.94 dBFS。これより上を soft knee 領域として ±1.0 に収束させる。
constexpr float kSoftClipThreshold = 0.8f;

// soft-clip（閾値以下は線形通過、閾値超過分のみ tanh で ±1.0 に圧縮）
inline float softClip(float v)
{
    const float a = std::fabs(v);
    if (a <= kSoftClipThreshold) return v;
    const float sign    = (v < 0.0f) ? -1.0f : 1.0f;
    const float ceiling = 1.0f - kSoftClipThreshold;
    const float over    = a - kSoftClipThreshold;
    return sign * (kSoftClipThreshold + ceiling * std::tanh(over / ceiling));
}

} // namespace

AudioWorker::AudioWorker(const QAudioFormat& format, QObject* parent)
    : QObject(parent)
    , m_format(format)
{
}

AudioWorker::~AudioWorker() = default;

void AudioWorker::start()
{
    // 所属スレッド（audio thread）で QAudioSink を生成して start する。
    // QAudioSink / QIODevice の thread affinity を所属スレッドで一貫させる必要があるため、
    // 必ず本スロット経由で生成し、コンストラクタ側では生成しない。
    // バッファサイズは setBufferSize で明示せず Qt 既定（プラットフォーム依存）に任せる。
    // Windows の WASAPI バックエンドでは数百 ms 規模のバッファが既定で確保されており、
    // ドラッグ中の途切れに対しては別スレッド分離（VideoView 側の構成）の方が支配的に効く
    m_sink    = new QAudioSink(m_format, this);
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "QAudioSink::start() failed:" << m_sink->error();
    }
}

void AudioWorker::onAudioBuffer(const QAudioBuffer& buf)
{
    if (!m_sinkDev) return;
    if (buf.format().sampleFormat() != QAudioFormat::Float) return;

    // Float サンプルに gain を線形乗算し、閾値超過分のみソフトクリップする。
    // 閾値以下のサンプルは線形通過（無歪み）、閾値超過分のみ tanh で ±1.0 付近に飽和させる。
    // 全サンプルに tanh を通す方式は小振幅でも歪みが乗るため、
    // 高 gain × playbackRate ≠ 1.0（resampler overshoot あり）の組み合わせで
    // 可聴ノイズが顕在化していた。
    const qsizetype n = buf.byteCount() / static_cast<qsizetype>(sizeof(float));
    if (n <= 0) return;
    // 端数バイト切り捨て対策
    // byteCount が sizeof(float) の倍数でない場合、bytes をそのまま使うと末尾の
    // 端数バイトが未初期化のまま sink に流れるため、整数倍に揃えてから書き込む
    const qsizetype outBytes = n * static_cast<qsizetype>(sizeof(float));
    const float* src = buf.constData<float>();
    QByteArray out(outBytes, Qt::Uninitialized);
    float* dst = reinterpret_cast<float*>(out.data());
    const float g = static_cast<float>(m_gain);
    for (qsizetype i = 0; i < n; ++i) {
        dst[i] = softClip(src[i] * g);
    }
    // 書き込みアンダーラン検出
    // 戻り値が要求バイト数より少ない場合、sink の内部バッファが飽和して書き込みを
    // 取りこぼしている。診断のため警告に出力する（リアルタイム再生のため再送はしない）
    const qint64 written = m_sinkDev->write(out);
    if (written < outBytes) {
        qWarning() << "QAudioSink write underrun:" << written << "/" << outBytes;
    }
}

void AudioWorker::reset()
{
    // ソース切替時に sink 内の積み残しサンプルを破棄して QIODevice を取り直す。
    // QAudioSink::reset() のみではプラットフォームによって停止状態に遷移しないことがあるため、
    // stop() で完全停止させてから start() を呼んで状態機械を確実にリセットする
    if (!m_sink) return;
    m_sink->stop();
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "QAudioSink::start() failed (after reset):" << m_sink->error();
    }
}

void AudioWorker::setGain(double gain)
{
    m_gain = gain;
}

void AudioWorker::teardown()
{
    // QAudioSink は audio thread で生成・所有しているため、ここ（audio thread）で
    // stop して delete する。後段の QThread::quit/wait で安全に終了させる
    if (!m_sink) return;
    m_sink->stop();
    delete m_sink;
    m_sink    = nullptr;
    m_sinkDev = nullptr;
}
