#pragma once
#include <cstddef>

// RMS コンプレッサ + メイクアップゲイン DSP
// 大声/小声の音量差を縮小する。出力サンプルは常に ±kLimiterCeil 以内に収まる。
// 過去の gain × playbackRate overshoot ノイズ問題を回避するため、自己ピーク制限設計とし
// soft-clip を排除している（limiter は通常時ほぼ不発、最悪ケースのハードキャップのみ機能する）
class Normalizer {
public:
    // initialEnabled で起動時の適用状態を確定する（アニメーションなし即時反映）
    explicit Normalizer(int sampleRate = 48000, int channels = 2, bool initialEnabled = true);

    // enabled 状態を変更する。変更は内部ランプで 50ms かけて滑らかに反映する
    void setEnabled(bool enabled);

    // シーク時の内部状態リセット（enabled 状態・applyRatio は維持する）
    // reset 直後は kRmsWindowMs（10ms）相当の warmup として currentGain=1.0 を据え置き、
    // 経過後に初回 recalc で targetGain を確定し、以降は kAttackMs/kReleaseMs で追従する。
    // warmup の目的はシーク直後に RMS 追跡器が真値に収束する前のターゲットゲイン算出を避け、
    // currentGain=1.0 → 適正値への急峻な立ち上がり（ポップノイズ）を抑えることにある
    void reset();

    // Float サンプル列を in-place で処理する（インターリーブ形式）
    // n はサンプル数（チャンネル数 × フレーム数）
    void process(float* samples, std::ptrdiff_t n);

private:
    int   m_channels;
    bool  m_enabled;
    float m_applyRatio;  // 0.0=バイパス完全, 1.0=DSP 完全適用
    float m_rampStep;    // 1 フレームあたりの applyRatio 変化量（50ms ランプ相当）

    float m_rmsState;      // RMS 追跡 IIR の状態（二乗平均値）
    float m_rmsCoeff;      // RMS IIR 係数（10ms 窓相当）

    float m_currentGain;   // 現在のコンプレッサゲイン（線形値）
    float m_attackCoeff;   // アタック IIR 係数（20ms）
    float m_releaseCoeff;  // リリース IIR 係数（250ms）

    // DSP 重数学関数の間引き計算用
    // sqrt/log10/pow をフレーム単位（48kHz）で実行すると audio thread が逼迫し underrun を招く。
    // RMS 窓 10ms に対応するフレーム数に 1 回だけターゲットゲインを再計算し、
    // アタック/リリース IIR は毎フレーム回して追従性を維持する
    float m_targetGain;          // 直近に計算した目標ゲイン（次の再計算まで保持）
    int   m_frameCounter;        // 再計算インターバル用カウンタ
    int   m_gainRecalcInterval;  // 再計算するフレーム間隔（RMS 窓に同期）
};
