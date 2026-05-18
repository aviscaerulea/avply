#pragma once
#include <array>
#include <cstddef>

// 音声明瞭化（Voice Clarity）DSP
// 直列 3 段 Biquad IIR フィルタ（HPF → Peaking EQ → High-shelf）で
// こもり気味の音声の子音帯域を持ち上げ、低域カブリを抑える。
// チャネル独立に状態を持ち、ON/OFF は 50ms ランプでブレンドして段差ノイズを回避する
class VoiceClarity {
public:
    // initialEnabled で起動時の適用状態を確定する（ランプなし即時反映）
    explicit VoiceClarity(int sampleRate = 48000, int channels = 2, bool initialEnabled = true);

    // enabled 状態を変更する。実際の反映は 50ms の線形ランプでブレンド比率を遷移させる
    void setEnabled(bool enabled);

    // シーク・ソース切替時の状態リセット（enabled / applyRatio は維持）
    // 各 Biquad 段のフィルタ遅延状態をゼロクリアし、シーク直後の不連続点を防ぐ
    void reset();

    // Float サンプル列を in-place で処理する（インターリーブ形式）
    // n はサンプル数（チャンネル数 × フレーム数）
    void process(float* samples, std::ptrdiff_t n);

    // Biquad 係数（a0 で正規化済み、a0=1 として保持）
    // VoiceClarity.cpp 内の係数算出ヘルパ（static 自由関数）が返り値型として参照するため
    // public 公開する。クラスメンバ関数ではないため private のままだとアクセスできない
    struct BiquadCoeff {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

private:
    // Biquad Direct Form I の状態（チャンネル単位、3 段分）
    // m_states メンバの型としてのみ使うため private に置く
    struct BiquadState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    int   m_channels;
    bool  m_enabled;
    float m_applyRatio;  // 0.0=バイパス, 1.0=DSP 完全適用
    float m_rampStep;    // 1 フレームあたりの applyRatio 変化量（50ms 相当）

    // 3 段の係数（全チャンネル共通、起動時に算出）
    std::array<BiquadCoeff, 3> m_coeffs;

    // チャンネル × 段の状態。最大 2 チャンネル × 3 段。
    // ステレオ前提だが将来モノラル/サラウンドに備えて 2ch 分まで確保する
    std::array<std::array<BiquadState, 3>, 2> m_states;
};
