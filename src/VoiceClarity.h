#pragma once
#include <array>
#include <cstddef>

// 音声明瞭化（Voice Clarity）DSP
// 直列 3 段 Biquad IIR フィルタ（HPF → Peaking EQ → High-shelf）で
// こもり気味の音声の子音帯域を持ち上げ、低域カブリを抑える。
// チャネル独立に状態を持ち、ON/OFF は 50ms ランプでブレンドして段差ノイズを回避する
class VoiceClarity {
public:
    // 適用強度
    // Off は完全バイパス（applyRatio→0）、Small/Medium/Large はピーク・シェルフのゲインを段階的に増やす。
    // HPF カットオフは強度に依らず一定（低域カブリ除去は常に同じ効果でよい）
    enum class Level {
        Off    = 0,
        Small  = 1,
        Medium = 2,
        Large  = 3,
    };

    // initialLevel で起動時の適用強度を確定する（ランプなし即時反映）
    explicit VoiceClarity(int sampleRate = 48000, int channels = 2, Level initialLevel = Level::Medium);

    // 適用強度を変更する
    // Off ↔ ON 遷移は 50ms ランプで applyRatio をブレンドする。
    // ON 状態間の強度変更（小↔中↔大）は係数のみ差し替え、フィルタ内部状態は維持する
    void setLevel(Level level);

    // 現在の適用強度を返す
    Level level() const { return m_level; }

    // シーク・ソース切替時の状態リセット（level / applyRatio は維持）
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

    // 指定強度に対応する Biquad 係数を再計算して m_coeffs に書き込む
    // Off の場合は何もしない（係数は維持、process 側でバイパス処理する）
    void recomputeCoeffs(Level level);

    float m_sampleRate;
    int   m_channels;
    Level m_level;
    float m_applyRatio;  // 0.0=バイパス, 1.0=DSP 完全適用
    float m_rampStep;    // 1 フレームあたりの applyRatio 変化量（50ms 相当）

    // 3 段の係数（全チャンネル共通、強度変更時に再算出）
    std::array<BiquadCoeff, 3> m_coeffs;

    // チャンネル × 段の状態。最大 2 チャンネル × 3 段。
    // ステレオ前提だが将来モノラル/サラウンドに備えて 2ch 分まで確保する
    std::array<std::array<BiquadState, 3>, 2> m_states;
};
