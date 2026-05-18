#include "VoiceClarity.h"
#include <algorithm>
#include <cassert>
#include <cmath>

// DSP パラメータ（直列 3 段の Biquad）
// RBJ Audio EQ Cookbook 準拠の係数算出を 48kHz 固定で起動時に 1 回実行する。
// 「こもり」は中高域不足と低域過多が原因のため、HPF + プレゼンスブースト + 高域シェルフで対応する

// 1 段目：High-pass フィルタ
// 100Hz 以下のランブル・ハンドリングノイズ・空調音をカットし、声の輪郭を浮かせる
static constexpr float kHpfFreq = 100.0f;
static constexpr float kHpfQ    = 0.707f;  // Butterworth 特性

// 2 段目：Peaking EQ
// 3kHz 中心の子音帯域（プレゼンス）を持ち上げてこもり感を軽減する
// ゲインは強度（Level）に応じて切り替える
static constexpr float kPeakFreq    = 3000.0f;
static constexpr float kPeakQ       = 1.0f;
static constexpr float kPeakDbSmall  = +3.0f;
static constexpr float kPeakDbMedium = +5.0f;
static constexpr float kPeakDbLarge  = +7.0f;

// 3 段目：High-shelf
// 8kHz 以上をシェルフブーストして高域の抜けを補い、明瞭感を底上げする
// ゲインは強度（Level）に応じて切り替える
static constexpr float kShelfFreq    = 8000.0f;
static constexpr float kShelfQ       = 0.707f;
static constexpr float kShelfDbSmall  = +1.0f;
static constexpr float kShelfDbMedium = +2.0f;
static constexpr float kShelfDbLarge  = +3.0f;

// ON/OFF トグル時のクロスフェード時間（ms）
// raw 信号と processed 信号のブレンド比を線形に切り替える。短すぎるとクリックノイズが出る
static constexpr float kRampMs = 50.0f;

// 円周率
// std::numbers::pi は C++20 機能のため、C++17 プロジェクトでは利用できず手書き定数で代替する
static constexpr float kPi = 3.14159265358979323846f;

// High-pass Biquad 係数（RBJ Cookbook）
// fc：カットオフ周波数、Q：Q 値
static VoiceClarity::BiquadCoeff makeHpfCoeff(float sampleRate, float fc, float q);

// Peaking EQ Biquad 係数（RBJ Cookbook）
// fc：中心周波数、Q：Q 値、gainDb：ピークゲイン（dB、正でブースト）
static VoiceClarity::BiquadCoeff makePeakCoeff(float sampleRate, float fc, float q, float gainDb);

// High-shelf Biquad 係数（RBJ Cookbook）
// fc：シェルフ周波数、Q：Q 値、gainDb：シェルフゲイン（dB、正でブースト）
static VoiceClarity::BiquadCoeff makeHighShelfCoeff(float sampleRate, float fc, float q, float gainDb);

// 強度 → Peak/Shelf ゲイン（dB）のペアを返す
// Off は呼ばれない前提（呼び出し側で Off 判定して係数差し替えをスキップする）
static void gainsForLevel(VoiceClarity::Level level, float& peakDb, float& shelfDb)
{
    // Off を渡すのはプログラマエラー。recomputeCoeffs 側でガード済みだが多重防衛する
    assert(level != VoiceClarity::Level::Off);

    switch (level) {
    case VoiceClarity::Level::Small:
        peakDb  = kPeakDbSmall;
        shelfDb = kShelfDbSmall;
        break;
    case VoiceClarity::Level::Large:
        peakDb  = kPeakDbLarge;
        shelfDb = kShelfDbLarge;
        break;
    case VoiceClarity::Level::Medium:
    default:
        peakDb  = kPeakDbMedium;
        shelfDb = kShelfDbMedium;
        break;
    }
}

VoiceClarity::VoiceClarity(int sampleRate, int channels, Level initialLevel)
    : m_sampleRate(static_cast<float>(sampleRate > 0 ? sampleRate : 48000))
    , m_channels(std::clamp(channels, 1, 2))
    , m_level(initialLevel)
    , m_applyRatio(initialLevel == Level::Off ? 0.0f : 1.0f)
{
    // sampleRate 不正値（0 や負）に対するゼロ除算ガード
    // 想定外の format でインスタンスが作られた場合でも 48kHz にフォールバックさせて安全に動かす
    m_rampStep = 1.0f / (m_sampleRate * kRampMs * 0.001f);

    // 初期係数：Off の場合は Medium 相当を仮置きする
    // process は Off + applyRatio==0 なら早期 return するため、Off 状態で係数が呼ばれることはない。
    // Off → 他強度への遷移時に setLevel 経由で再計算される
    recomputeCoeffs(initialLevel == Level::Off ? Level::Medium : initialLevel);
}

void VoiceClarity::setLevel(Level level)
{
    if (level == m_level) return;

    // Off → ON または ON 強度変更時のみ係数を更新する
    // ON → Off は係数を維持し、applyRatio のランプ収束で完全バイパスに移行する
    if (level != Level::Off) {
        recomputeCoeffs(level);
    }
    m_level = level;
}

void VoiceClarity::reset()
{
    // 全チャンネル × 全段の Biquad 内部状態をゼロクリアする。
    // シーク・ファイル切替後に旧サンプルの遅延が混入してインパルス的なポップが出るのを防ぐ
    for (auto& ch : m_states) {
        for (auto& st : ch) {
            st = BiquadState{};
        }
    }
}

void VoiceClarity::process(float* samples, std::ptrdiff_t n)
{
    const std::ptrdiff_t frames = n / m_channels;

    // 完全バイパス：Off かつランプ収束済み（m_applyRatio == 0）の場合は Biquad 計算ごとスキップする。
    // 出力は raw 入力そのままで in-place 不変。次回 ON 時は古い Biquad 状態のまま再開するが、
    // m_applyRatio が 0→1 へ 50ms かけて遷移する間に過渡応答も raw 寄りでマスクされ聴感影響は出ない
    if (m_level == Level::Off && m_applyRatio == 0.0f) {
        return;
    }

    const bool enabled = (m_level != Level::Off);
    for (std::ptrdiff_t f = 0; f < frames; ++f) {
        // バイパスランプ更新（ON→1.0, Off→0.0 へ線形補間）
        if (enabled) {
            m_applyRatio = std::min(1.0f, m_applyRatio + m_rampStep);
        }
        else {
            m_applyRatio = std::max(0.0f, m_applyRatio - m_rampStep);
        }

        float* frame = samples + f * m_channels;
        for (int ch = 0; ch < m_channels; ++ch) {
            const float raw = frame[ch];
            float x = raw;

            // 3 段カスケード処理
            // 各段の出力を次段の入力にする。状態 (x1,x2,y1,y2) はチャンネル × 段ごとに独立
            for (int stage = 0; stage < 3; ++stage) {
                const auto& c  = m_coeffs[stage];
                auto&       st = m_states[ch][stage];
                const float y = c.b0 * x + c.b1 * st.x1 + c.b2 * st.x2
                              - c.a1 * st.y1 - c.a2 * st.y2;
                st.x2 = st.x1;
                st.x1 = x;
                st.y2 = st.y1;
                st.y1 = y;
                x = y;
            }

            // raw と processed のブレンド出力（applyRatio で重み付け）
            frame[ch] = m_applyRatio * x + (1.0f - m_applyRatio) * raw;
        }
    }
}

void VoiceClarity::recomputeCoeffs(Level level)
{
    float peakDb  = 0.0f;
    float shelfDb = 0.0f;
    gainsForLevel(level, peakDb, shelfDb);

    m_coeffs[0] = makeHpfCoeff      (m_sampleRate, kHpfFreq,   kHpfQ);
    m_coeffs[1] = makePeakCoeff     (m_sampleRate, kPeakFreq,  kPeakQ,  peakDb);
    m_coeffs[2] = makeHighShelfCoeff(m_sampleRate, kShelfFreq, kShelfQ, shelfDb);
}

static VoiceClarity::BiquadCoeff makeHpfCoeff(float sampleRate, float fc, float q)
{
    const float w0    = 2.0f * kPi * fc / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float b0 =  (1.0f + cosw0) * 0.5f;
    const float b1 = -(1.0f + cosw0);
    const float b2 =  (1.0f + cosw0) * 0.5f;
    const float a0 =   1.0f + alpha;
    const float a1 =  -2.0f * cosw0;
    const float a2 =   1.0f - alpha;

    VoiceClarity::BiquadCoeff c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

static VoiceClarity::BiquadCoeff makePeakCoeff(float sampleRate, float fc, float q, float gainDb)
{
    const float A     = std::pow(10.0f, gainDb / 40.0f);
    const float w0    = 2.0f * kPi * fc / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);

    const float b0 =  1.0f + alpha * A;
    const float b1 = -2.0f * cosw0;
    const float b2 =  1.0f - alpha * A;
    const float a0 =  1.0f + alpha / A;
    const float a1 = -2.0f * cosw0;
    const float a2 =  1.0f - alpha / A;

    VoiceClarity::BiquadCoeff c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

static VoiceClarity::BiquadCoeff makeHighShelfCoeff(float sampleRate, float fc, float q, float gainDb)
{
    const float A     = std::pow(10.0f, gainDb / 40.0f);
    const float w0    = 2.0f * kPi * fc / sampleRate;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * q);
    const float sqrtA = std::sqrt(A);

    const float b0 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
    const float b2 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
    const float a0 =             (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
    const float a1 =      2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
    const float a2 =             (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;

    VoiceClarity::BiquadCoeff c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}
