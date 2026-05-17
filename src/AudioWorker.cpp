#include "AudioWorker.h"
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <SoundTouch.h>

AudioWorker::AudioWorker(const QAudioFormat& format, bool initialNormalize, QObject* parent)
    : QObject(parent)
    , m_format(format)
    , m_normalizer(format.sampleRate(), format.channelCount(), initialNormalize)
{
}

AudioWorker::~AudioWorker() = default;

void AudioWorker::start()
{
    // 所属スレッド（audio thread）で QAudioSink を生成して start する。
    // QAudioSink / QIODevice の thread affinity を所属スレッドで一貫させるため、
    // コンストラクタ側では生成せず必ず本スロット経由で生成する
    m_sink = new QAudioSink(m_format, this);

    // 内部バッファを 200ms 相当に拡張する（48kHz stereo Float の場合 76800 バイト）。
    // デフォルトの WASAPI バッファは数 ms と極小で、1.5x 等の高速再生時にデコーダから
    // バーストで届くサンプルを吸収しきれず write が partial になり音が欠ける。
    // setBufferSize は start() より前に呼ぶ必要がある
    const qsizetype bytesPerSec =
        static_cast<qsizetype>(m_format.bytesForDuration(1000 * 1000));
    m_sink->setBufferSize(bytesPerSec / 5);

    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed:" << m_sink->error();
    }
    qInfo() << "AudioWorker: QAudioSink bufferSize="
            << m_sink->bufferSize()
            << "bytes (requested" << (bytesPerSec / 5) << ")";

    // SoundTouch を所属スレッドで生成する。
    // QAudioBufferOutput が pitchCompensation を無視するため、AudioWorker 側で
    // playback rate に応じた時間圧縮 / 伸長を行う。setTempo は setPlaybackRate スロット経由で更新する
    m_stretch = std::make_unique<soundtouch::SoundTouch>();
    m_stretch->setSampleRate(static_cast<uint>(m_format.sampleRate()));
    m_stretch->setChannels(static_cast<uint>(m_format.channelCount()));
    m_stretch->setTempo(1.0);
}

void AudioWorker::onAudioBuffer(const QAudioBuffer& buf)
{
    if (!m_sinkDev || !m_stretch) return;

    // 診断ログ: 初回バッファのフォーマットを記録する（format mismatch 検出用）
    static bool s_firstReported = false;
    if (!s_firstReported) {
        s_firstReported = true;
        const auto& f = buf.format();
        qInfo() << "AudioWorker: first buffer format:"
                << "rate=" << f.sampleRate()
                << "ch=" << f.channelCount()
                << "fmt=" << static_cast<int>(f.sampleFormat())
                << "frames=" << buf.frameCount()
                << "bytes=" << buf.byteCount();
    }

    if (buf.format().sampleFormat() != QAudioFormat::Float) return;

    const int channels = m_format.channelCount();
    const qsizetype inFrames = buf.frameCount();
    if (inFrames <= 0) return;

    // SoundTouch に入力サンプル（interleaved float）を投入する
    // 出力は tempo に応じて入力 frame 数 / tempo 程度のフレーム数になる
    m_stretch->putSamples(buf.constData<float>(), static_cast<uint>(inFrames));

    // SoundTouch から time-stretched サンプルを取り出して DSP・音量・sink への書き込みを行う
    // 1 回の receiveSamples で全部出ない場合があるため received == 0 までループする
    constexpr uint kRecvBatchFrames = 4096;
    const qsizetype batchBytes =
        static_cast<qsizetype>(kRecvBatchFrames) * channels * static_cast<qsizetype>(sizeof(float));
    if (m_workBuf.size() < batchBytes) {
        m_workBuf.resize(batchBytes);
    }
    float* recv = reinterpret_cast<float*>(m_workBuf.data());

    qint64 totalInBytes  = 0;
    qint64 totalOutBytes = 0;
    int    underruns     = 0;
    int    writes        = 0;
    for (;;) {
        const uint received = m_stretch->receiveSamples(recv, kRecvBatchFrames);
        if (received == 0) break;

        const qsizetype outSamples = static_cast<qsizetype>(received) * channels;
        const qsizetype outBytes   = outSamples * static_cast<qsizetype>(sizeof(float));

        m_normalizer.process(recv, outSamples);

        // 音量を線形乗算する（vol は setVolume で 0.0〜1.0 にクランプ済みのため追加クリップは不要）
        const float vol = static_cast<float>(m_volume);
        for (qsizetype i = 0; i < outSamples; ++i) {
            recv[i] *= vol;
        }

        const qint64 written = m_sinkDev->write(m_workBuf.constData(), outBytes);
        if (written < outBytes) ++underruns;
        totalInBytes  += outBytes;
        totalOutBytes += (written > 0 ? written : 0);
        ++writes;
    }

    // 診断ログ: 1秒ごとに集計（毎呼び出し underrun を出すと音飛びの体感悪化に繋がるため）
    static qint64 s_winStart = 0;
    static qint64 s_inBytes  = 0;
    static qint64 s_outBytes = 0;
    static qint64 s_writes   = 0;
    static qint64 s_under    = 0;
    s_inBytes  += totalInBytes;
    s_outBytes += totalOutBytes;
    s_writes   += writes;
    s_under    += underruns;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (s_winStart == 0) s_winStart = now;
    if (now - s_winStart >= 1000) {
        qInfo() << "AudioWorker: 1s stats"
                << "in=" << (s_inBytes / 1024) << "KB"
                << "out=" << (s_outBytes / 1024) << "KB"
                << "writes=" << s_writes
                << "underruns=" << s_under
                << "bytesFree=" << (m_sink ? m_sink->bytesFree() : -1)
                << "tempo=" << m_stretch->getInputOutputSampleRatio();
        s_winStart = now;
        s_inBytes  = 0;
        s_outBytes = 0;
        s_writes   = 0;
        s_under    = 0;
    }
}

void AudioWorker::reset()
{
    // ソース切替・シーク時に sink のバッファを破棄して Normalizer・SoundTouch 状態もリセットする。
    // QAudioSink::reset() のみでは停止状態への遷移が保証されないため
    // stop() → start() で状態機械を確実にリセットする
    m_normalizer.reset();
    if (m_stretch) m_stretch->clear();
    // ソース切替時にバッファを解放する（次の onAudioBuffer で必要サイズに再確保される）
    m_workBuf.clear();
    if (!m_sink) return;
    m_sink->stop();
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed (after reset):" << m_sink->error();
    }
}

void AudioWorker::setVolume(double volume)
{
    m_volume = volume;
}

void AudioWorker::setNormalizeEnabled(bool enabled)
{
    m_normalizer.setEnabled(enabled);
}

void AudioWorker::setPlaybackRate(double rate)
{
    // SoundTouch の tempo に再生速度を反映する。
    // tempo=1.5 → 入力 1.5 秒分を出力 1 秒分に時間圧縮（ピッチ保持）
    // 範囲外 / 不正値は無視する（0 や負値は SoundTouch の前提を破る）
    if (!m_stretch) return;
    if (rate <= 0.0) return;
    m_stretch->setTempo(rate);
}

void AudioWorker::teardown()
{
    // audio thread 上で QAudioSink・SoundTouch を安全に破棄する。
    // QAudioSink は audio thread で生成しているため GUI thread からの delete は thread affinity 違反になる
    if (m_sink) {
        m_sink->stop();
        m_sink = nullptr;
        m_sinkDev = nullptr;
    }
    m_stretch.reset();
}
