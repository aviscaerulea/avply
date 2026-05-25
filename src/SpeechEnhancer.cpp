#include "SpeechEnhancer.h"

#include <webrtc/modules/audio_processing/include/audio_processing.h>

#include <QDebug>
#include <algorithm>
#include <vector>

namespace {

// 出力 FIFO 圧縮閾値
// 取り出し済みプレフィックスがこのサンプル数を超えたら先頭詰めして再確保を抑える。
constexpr qsizetype kOutCompactThreshold = 48000; // 約 1 秒（48kHz interleaved 換算で十分小さい）

// NS レベル変換
// 0〜3 を webrtc の Level enum へクランプして対応付ける。
webrtc::AudioProcessing::Config::NoiseSuppression::Level nsLevelFromInt(int v)
{
    using NS = webrtc::AudioProcessing::Config::NoiseSuppression;
    switch (std::clamp(v, 0, 3)) {
    case 0:
        return NS::kLow;
    case 1:
        return NS::kModerate;
    case 2:
        return NS::kHigh;
    default:
        return NS::kVeryHigh;
    }
}

} // namespace

// SpeechEnhancer 実装本体
// webrtc ヘッダの露出を本ファイルに限定するため pimpl とする。
struct SpeechEnhancer::Impl {
    int sampleRate;
    int channels;
    int frameSize; // sampleRate / 100（10ms 分の 1ch サンプル数）
    Level level;
    LevelParams params[3]; // index 0=Low, 1=Medium, 2=High

    rtc::scoped_refptr<webrtc::AudioProcessing> apm;
    webrtc::StreamConfig monoConfig; // (sampleRate, 1ch)

    std::vector<float> monoFrame;    // APM 入力用モノラル作業領域
    std::vector<float> monoFrameOut; // APM 出力用モノラル作業領域（in-place 禁止のため入出力を分離）
    std::vector<float> inMono;    // ダウンミックス済みで未処理のモノラルサンプル
    std::vector<float> outBuf;    // 処理済み interleaved 出力 FIFO
    qsizetype outRead = 0;

    // レベル別 Config 構築
    // Off 以外で呼ぶ前提（呼び出し側で level != Off をガード済み）。NS / AGC2 / HPF を有効化し、AEC は使わない（再生済み音声のため）。
    webrtc::AudioProcessing::Config buildConfig(Level lvl) const
    {
        // params は Low/Medium/High の 3 要素。Level は Off=0/Low=1/Medium=2/High=3 のため -1 で index へ写す
        const LevelParams& p = params[static_cast<int>(lvl) - 1];
        webrtc::AudioProcessing::Config c;
        c.pipeline.maximum_internal_processing_rate = 48000;
        c.high_pass_filter.enabled = true;
        c.noise_suppression.enabled = true;
        c.noise_suppression.level = nsLevelFromInt(p.noiseSuppressionLevel);
        c.gain_controller2.enabled = true;
        c.gain_controller2.adaptive_digital.enabled = true;
        c.gain_controller2.adaptive_digital.max_gain_db = p.maxGainDb;
        c.gain_controller2.fixed_digital.gain_db = p.fixedGainDb;
        return c;
    }

    // interleaved 1 フレームのモノラルダウンミックス
    // channels 全チャンネルの平均を返す。
    float downmix(const float* frame) const
    {
        if (channels == 1) {
            return frame[0];
        }
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            sum += frame[c];
        }
        return sum / static_cast<float>(channels);
    }

    // 出力 FIFO 圧縮
    // 取り出し済みプレフィックスを破棄して先頭へ詰める。
    void compactOut()
    {
        if (outRead == 0) {
            return;
        }
        const qsizetype remain = std::max<qsizetype>(0, static_cast<qsizetype>(outBuf.size()) - outRead);
        if (remain > 0) {
            std::move(outBuf.begin() + outRead, outBuf.end(), outBuf.begin());
        }
        outBuf.resize(static_cast<size_t>(remain));
        outRead = 0;
    }
};

// コンストラクタ
// APM を生成し、初期レベルが Off 以外なら Config を適用して初期化する。
SpeechEnhancer::SpeechEnhancer(int sampleRate, int channels,
                               const LevelParams& low,
                               const LevelParams& medium,
                               const LevelParams& high,
                               Level initialLevel)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->sampleRate = sampleRate;
    m_impl->channels = channels;
    m_impl->frameSize = sampleRate / 100;
    m_impl->level = initialLevel;
    m_impl->params[0] = low;
    m_impl->params[1] = medium;
    m_impl->params[2] = high;
    m_impl->monoConfig = webrtc::StreamConfig(sampleRate, 1);
    m_impl->monoFrame.resize(static_cast<size_t>(m_impl->frameSize));
    m_impl->monoFrameOut.resize(static_cast<size_t>(m_impl->frameSize));

    m_impl->apm = webrtc::AudioProcessingBuilder().Create();
    if (!m_impl->apm) {
        qWarning() << "SpeechEnhancer: AudioProcessingBuilder().Create() が失敗しました。Off（素通し）で動作します。";
    }
    if (m_impl->apm && initialLevel != Level::Off) {
        m_impl->apm->ApplyConfig(m_impl->buildConfig(initialLevel));
        m_impl->apm->Initialize();
    }
}

SpeechEnhancer::~SpeechEnhancer() = default;

// レベル変更
// 未処理の端数モノラルサンプルを破棄（<10ms）し、Off 以外なら Config を適用する。
// 素通しからの復帰時は内部状態を初期化して過去のゲイン追従を持ち越さない。
void SpeechEnhancer::setLevel(Level level)
{
    if (level == m_impl->level) {
        return;
    }
    const bool wasOff = (m_impl->level == Level::Off);
    m_impl->level = level;
    m_impl->inMono.clear();

    if (level != Level::Off && m_impl->apm) {
        m_impl->apm->ApplyConfig(m_impl->buildConfig(level));
        if (wasOff) {
            // Off → On 遷移時：素通しで積まれた outBuf を破棄し、AGC 追従状態をリセットする。
            // outBuf を残すと素通しサンプルと APM 処理済みサンプルが混在してポップノイズになる
            m_impl->outBuf.clear();
            m_impl->outRead = 0;
            m_impl->apm->Initialize();
        }
    }
}

SpeechEnhancer::Level SpeechEnhancer::level() const
{
    return m_impl->level;
}

// interleaved サンプル投入
// Off は素通しでそのまま出力 FIFO へ。ON はモノラル化して 10ms フレーム単位に APM 処理する。
void SpeechEnhancer::pushInterleaved(const float* in, qsizetype frames)
{
    if (frames <= 0) {
        return;
    }

    if (m_impl->level == Level::Off || !m_impl->apm) {
        const qsizetype n = frames * m_impl->channels;
        m_impl->outBuf.insert(m_impl->outBuf.end(), in, in + n);
        return;
    }

    const int ch = m_impl->channels;
    const int fs = m_impl->frameSize;

    for (qsizetype i = 0; i < frames; ++i) {
        m_impl->inMono.push_back(m_impl->downmix(in + i * ch));
    }

    // 10ms フレーム単位の APM 処理
    // filled はループ外で固定する。今回 push したサンプルも含めた全蓄積から fs（480）刻みで取り出す
    qsizetype pos = 0;
    const qsizetype filled = static_cast<qsizetype>(m_impl->inMono.size());
    while (filled - pos >= fs) {
        std::copy(m_impl->inMono.begin() + pos,
                  m_impl->inMono.begin() + pos + fs,
                  m_impl->monoFrame.begin());

        float* inPtr  = m_impl->monoFrame.data();
        float* outPtr = m_impl->monoFrameOut.data();
        const int err = m_impl->apm->ProcessStream(&inPtr, m_impl->monoConfig, m_impl->monoConfig, &outPtr);
        if (err != 0) {
            qWarning() << "SpeechEnhancer: ProcessStream が失敗しました。エラーコード：" << err;
            std::fill(m_impl->monoFrameOut.begin(), m_impl->monoFrameOut.end(), 0.0f);
        }

        for (int j = 0; j < fs; ++j) {
            const float s = m_impl->monoFrameOut[static_cast<size_t>(j)];
            for (int c = 0; c < ch; ++c) {
                m_impl->outBuf.push_back(s);
            }
        }
        pos += fs;
    }

    // 端数（<fs）を次回へ持ち越し
    // 残留は最大 fs-1 サンプルのため erase の先頭詰めコストは無視できる
    if (pos > 0) {
        m_impl->inMono.erase(m_impl->inMono.begin(), m_impl->inMono.begin() + pos);
    }
}

// 処理済み interleaved サンプル取り出し
qsizetype SpeechEnhancer::pullInterleaved(float* out, qsizetype maxFrames)
{
    const int ch = m_impl->channels;
    const qsizetype avail = availableFrames();
    const qsizetype n = std::min(avail, maxFrames);
    if (n <= 0) {
        return 0;
    }

    const float* src = m_impl->outBuf.data() + m_impl->outRead;
    std::copy(src, src + n * ch, out);
    m_impl->outRead += n * ch;

    if (m_impl->outRead >= kOutCompactThreshold) {
        m_impl->compactOut();
    }
    return n;
}

qsizetype SpeechEnhancer::availableFrames() const
{
    const qsizetype remain = std::max<qsizetype>(0, static_cast<qsizetype>(m_impl->outBuf.size()) - m_impl->outRead);
    return remain / m_impl->channels;
}

// 内部状態クリア
// APM を Initialize し蓄積 / 出力バッファを破棄する。
void SpeechEnhancer::reset()
{
    if (m_impl->apm) {
        m_impl->apm->Initialize();
    }
    m_impl->inMono.clear();
    m_impl->outBuf.clear();
    m_impl->outRead = 0;
}
