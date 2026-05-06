#include "AudioWorker.h"
#include <cmath>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QDebug>

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
    // 必ず本スロット経由で生成し、コンストラクタ側では生成しない
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

    // Float サンプルに gain を線形乗算し tanhf で ±1.0 付近に滑らかに飽和させる（ソフトクリップ）。
    // ハードクリップ（直角カット）は高調波歪み（ザリザリ音）を生むため避ける。
    // 特に playbackRate 変更時の resampler は overshoot サンプル（±1.0 超）を生成するため
    // gain 1.0 でもクリップ経路を通る場合に問題化する
    const qsizetype bytes = buf.byteCount();
    const qsizetype n = bytes / static_cast<qsizetype>(sizeof(float));
    const float* src = buf.constData<float>();
    QByteArray out(bytes, Qt::Uninitialized);
    float* dst = reinterpret_cast<float*>(out.data());
    const float g = static_cast<float>(m_gain);
    for (qsizetype i = 0; i < n; ++i) {
        dst[i] = std::tanh(src[i] * g);
    }
    m_sinkDev->write(out);
}

void AudioWorker::reset()
{
    // ソース切替時に sink 内の積み残しサンプルを破棄して、QIODevice を取り直す
    if (!m_sink) return;
    m_sink->reset();
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
