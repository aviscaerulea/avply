#pragma once
#include <cstddef>

// アップワード RMS コンプレッサ DSP
// 閾値未満の小音量のみを持ち上げ、閾値以上の大音量は素通しする（ゲイン 1.0）。
// 大音量を一切押し上げないため出力は元のピークを超えず、soft-limiter は通常時ほぼ不発で
// フルスケール直前のピークと後段 overshoot のみ穏やかに頭打ちする安全網として機能する。
// 出力サンプルは常に ±kLimiterCeil 以内に収まる
class Normalizer {
public:
    // 圧縮強度レベル
    // Off=バイパス、Small/Medium/Large は threshold と makeup gain を段階的に変える。
    // 値は Settings::normalizeLevel と 1:1 対応する（int 永続化のため）
    enum class Level : int {
        Off    = 0,
        Small  = 1,
        Medium = 2,
        Large  = 3,
    };

    // 強度ごとに切り替える DSP パラメータ
    // 「持ち上げ境界」と「最大ブースト量」を独立に指定する。Ratio / Attack / Release / Limiter 上限 /
    // RMS 窓は全強度共通（質感の一貫性を保つため）。avply.toml の [normalizer] セクションで
    // ユーザが調整できる
    struct LevelParams {
        float thresholdDb;  // 持ち上げ境界の RMS 閾値（dBFS、これ未満を持ち上げ以上は素通し、典型 -30〜-15）
        float makeupDb;     // 小音量ブーストの最大量（dB、典型 0〜+18）
    };

    // 初期強度と Small/Medium/Large の DSP パラメータを指定する。
    // Off は内部的に Medium パラメータを仮置き（process でバイパスするため未使用）
    explicit Normalizer(int sampleRate,
                        int channels,
                        Level initialLevel,
                        const LevelParams& small,
                        const LevelParams& medium,
                        const LevelParams& large);

    // 強度を変更する。Off↔ON 遷移は内部ランプで 50ms かけて滑らかに反映する。
    // ON 状態間（Small↔Medium↔Large）の変更は threshold / makeup の差し替えのみで
    // ランプは発生しない（圧縮特性が滑らかに変化するため聴感上のクリックは出ない）。
    // Off → ON 遷移時は Off 中に凍結していた targetGain を 1.0f にリセットして再 warmup する
    void setLevel(Level level);

    // シーク時の内部状態リセット（level・applyRatio は維持する）
    // reset 直後は kRmsWindowMs（10ms）相当の warmup として currentGain=1.0 を据え置き、
    // 経過後に初回 recalc で targetGain を確定し、以降は kAttackMs/kReleaseMs で追従する。
    // warmup の目的はシーク直後に RMS 追跡器が真値に収束する前のターゲットゲイン算出を避け、
    // currentGain=1.0 → 適正値への急峻な立ち上がり（ポップノイズ）を抑えることにある
    void reset();

    // Float サンプル列を in-place で処理する（インターリーブ形式）
    // n はサンプル数（チャンネル数 × フレーム数）
    void process(float* samples, std::ptrdiff_t n);

private:
    // 強度別パラメータ表。setLevel で添字参照して現在値を差し替える
    // 添字は Level enum の int 値（Off は未使用、Medium 値で埋める）
    LevelParams m_levelParams[4];

    int   m_channels;
    Level m_level;
    float m_applyRatio;  // 0.0=バイパス完全, 1.0=DSP 完全適用
    float m_rampStep;    // 1 フレームあたりの applyRatio 変化量（50ms ランプ相当）

    // 現在適用中のパラメータ（m_level に対応する m_levelParams の値）
    // setLevel 時に更新し、process ループ内では本変数を直接参照する
    float m_thresholdDb;
    float m_makeupDb;

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
