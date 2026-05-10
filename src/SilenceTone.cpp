#include "SilenceTone.h"
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QtMath>
#include <QDebug>
#include <cmath>

namespace {

// 連続トーン生成 QIODevice
// QAudioSink::start(QIODevice*) のプル方式で要求バイト数だけサンプルを生成する。
// 内部位相 m_phase を保持することでフレーム境界での不連続を回避する
class ToneDevice : public QIODevice {
public:
    ToneDevice(int sampleRate, int channels, double freq, double amp)
        : m_sampleRate(sampleRate)
        , m_channels(channels)
        , m_phaseStep(2.0 * M_PI * freq / static_cast<double>(sampleRate))
        , m_amp(amp)
    {
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override
    {
        // S16LE 想定。1 フレーム = チャンネル数 × 2 バイト
        const qint64 frameBytes = static_cast<qint64>(m_channels) * 2;
        const qint64 frames = maxlen / frameBytes;
        for (qint64 i = 0; i < frames; ++i) {
            const double v = std::sin(m_phase) * m_amp;
            const qint16 s = static_cast<qint16>(v * 32767.0);
            qint16* p = reinterpret_cast<qint16*>(data + i * frameBytes);
            for (int c = 0; c < m_channels; ++c) {
                p[c] = s;
            }
            m_phase += m_phaseStep;
            if (m_phase >= 2.0 * M_PI) m_phase -= 2.0 * M_PI;
        }
        return frames * frameBytes;
    }

    qint64 writeData(const char*, qint64) override { return 0; }

    bool isSequential() const override { return true; }

private:
    int    m_sampleRate;
    int    m_channels;
    double m_phaseStep;
    double m_amp;
    double m_phase = 0.0;
};

} // namespace

SilenceTone::SilenceTone(QObject* parent)
    : QObject(parent)
{
}

SilenceTone::~SilenceTone()
{
    stop();
}

void SilenceTone::start()
{
    if (m_sink) return;

    // 互換性重視で 48 kHz / Int16 / Stereo を要求する。
    // 大半の OS / BT スタックがネイティブ対応する形式
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(fmt)) {
        qDebug() << "SilenceTone: skipped (device unavailable or unsupported format)";
        return;
    }

    // 1 kHz / -72 dB 相当（振幅 0.00025）の不可聴トーン
    // 1 kHz は SBC 等の高域カット影響を受けず、BT パイプラインを確実に「アクティブ」状態に保つ周波数
    // 振幅は 16bit フルスケールの約 1/4000 で実用環境では知覚できない
    // m_device を先に確立し、以降の処理で例外が起きても stop() でクリーンアップできるようにする
    auto* tone = new ToneDevice(fmt.sampleRate(), fmt.channelCount(), 1000.0, 0.00025);
    tone->open(QIODevice::ReadOnly);
    m_device = tone;

    // parent を nullptr にして stop() が唯一の所有者となる（parent 経由の自動削除と deleteLater の二重破棄を防ぐ）
    m_sink = new QAudioSink(dev, fmt, nullptr);
    // バッファアンダーフロー対策で大きめに確保する（約 100 ms 分）。
    // OS スケジューリング遅延でプル要求が遅延しても無音になりにくい
    m_sink->setBufferSize(fmt.sampleRate() * fmt.channelCount() * 2 / 10);
    m_sink->start(tone);
}

void SilenceTone::stop()
{
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_device) {
        m_device->close();
        m_device->deleteLater();
        m_device = nullptr;
    }
}
