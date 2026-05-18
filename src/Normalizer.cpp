#include "Normalizer.h"
#include <cmath>
#include <algorithm>

// 圧縮開始の RMS 閾値（dBFS）
// この値を超えた帯域が圧縮対象。-25 dBFS は通常の発話・楽音より上に置き、暗騒音は素通しにする狙い
static constexpr float kThresholdDb = -25.0f;
// 圧縮比（threshold 超過分を 1/N にする）
// 6:1 は強めのダウンワード圧縮。大声側を確実に押し込むためにライブ放送系の典型値より深めに取る
static constexpr float kRatio       = 6.0f;
// コンプレッサ後のメイクアップゲイン（dB）
// 小声側の持ち上げ量。+10 dB で threshold 未満の信号は素通しのまま +10 dB 増幅される
static constexpr float kMakeupDb    = +10.0f;
// ハードリミッタの絶対上限（線形振幅、1.0=フルスケール）
// 0.97 は -0.26 dBFS。後段の resampler overshoot（最大 ~2%）を見込んでも 0.99 で頭打ちになる安全係数
static constexpr float kLimiterCeil = 0.97f;
// RMS 検出窓長（ms）
// 入力信号の実効値追跡に使う移動平均長。短すぎると発話の母音切れ目に過剰反応し、長すぎると応答が鈍る
static constexpr float kRmsWindowMs = 10.0f;
// ゲイン低下速度（ms）
// Attack 時定数。大声検出から目標ゲインへ収束するまでの IIR 時定数
static constexpr float kAttackMs    = 20.0f;
// ゲイン回復速度（ms）
// Release 時定数。無音・小声移行から目標ゲインへ戻るまでの IIR 時定数。聴感上のポンピング抑制のため Attack より長く取る
static constexpr float kReleaseMs   = 250.0f;
// ON/OFF トグル時の遷移時間（ms）
// raw 信号と processed 信号のブレンド比を線形に切り替える時間。短すぎるとクリック音が出る
static constexpr float kRampMs      = 50.0f;

// 1-pole IIR フィルタ係数
// 時定数 timeMs で指数的に追従する係数。
// alpha = exp(-1 / (rate * T)) を使い、update: y = alpha*y + (1-alpha)*x で時定数 T を実現する
static float iirCoeff(float rate, float timeMs)
{
    return std::exp(-1.0f / (rate * timeMs * 0.001f));
}

// RMS 追跡器の初期値（mean square = amplitude^2）
// 閾値 kThresholdDb 相当の振幅を二乗して与える。
// 初回フレームで levelDb が -∞ に振れる事態を防ぎ、初回 recalc 時点で
// 真の RMS への収束をより速く始められるよう、ありえそうな信号レベル付近に
// プリセットしておく
static const float kInitialRmsState = []() {
    const float amp = std::pow(10.0f, kThresholdDb / 20.0f);
    return amp * amp;
}();

Normalizer::Normalizer(int sampleRate, int channels, bool initialEnabled)
    : m_channels(std::max(1, channels))
    , m_enabled(initialEnabled)
    , m_applyRatio(initialEnabled ? 1.0f : 0.0f)
    , m_rmsState(kInitialRmsState)
    , m_currentGain(1.0f)
    , m_targetGain(1.0f)
{
    // フレームレートによる各係数の初期化
    // インターリーブ形式でも 1 フレーム = 全チャンネル 1 組であり、
    // フレームレートは sampleRate そのもの（stereo でも 48000 frames/sec）
    const float frameRate = static_cast<float>(sampleRate);
    m_rmsCoeff    = iirCoeff(frameRate, kRmsWindowMs);
    m_attackCoeff = iirCoeff(frameRate, kAttackMs);
    m_releaseCoeff = iirCoeff(frameRate, kReleaseMs);
    m_rampStep    = 1.0f / (frameRate * kRampMs * 0.001f);

    // 再計算インターバル = RMS 窓相当のフレーム数（最低 1）
    // 48kHz の場合 48000 fps × 0.01s = 480 フレームに 1 回
    m_gainRecalcInterval = std::max(1, static_cast<int>(frameRate * kRmsWindowMs * 0.001f));
    // 初回 recalc を 1 窓分（kRmsWindowMs）遅らせるためカウンタを 0 から開始する。
    // 即時 recalc にすると RMS 追跡器が真値に収束する前にターゲットゲインが算出され、
    // currentGain=1.0 → makeup 適用後ゲインへ kAttackMs かけて急峻に立ち上がるため
    // シーク直後にポップノイズとして体感される
    m_frameCounter = 0;
}

void Normalizer::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void Normalizer::reset()
{
    // シーク後に古いコンプレッサ状態が残らないようリセットする。
    // 再生再開直後は kRmsWindowMs（10ms）相当の warmup として currentGain=1.0 を据え置き、
    // 経過後に初回 recalc で targetGain を確定して以降 kAttackMs/kReleaseMs で追従する
    m_rmsState     = kInitialRmsState;
    m_currentGain  = 1.0f;
    m_targetGain   = 1.0f;
    // reset 後 1 窓分（kRmsWindowMs）は再計算を待ち、その間 currentGain は 1.0 のまま据え置く。
    // 即時再計算するとシーク直後にポップノイズが乗る（コンストラクタの該当コメント参照）
    m_frameCounter = 0;
}

void Normalizer::process(float* samples, std::ptrdiff_t n)
{
    const std::ptrdiff_t frames = n / m_channels;

    for (std::ptrdiff_t f = 0; f < frames; ++f) {
        float* frame = samples + f * m_channels;

        // チャンネル平均二乗値で RMS レベルを追跡する（ステレオは L+R 平均）
        float sumSq = 0.0f;
        for (int ch = 0; ch < m_channels; ++ch) {
            sumSq += frame[ch] * frame[ch];
        }
        m_rmsState = m_rmsCoeff * m_rmsState
                   + (1.0f - m_rmsCoeff) * (sumSq / m_channels);

        // ターゲットゲインの再計算（sqrt/log10/pow を含む重処理は RMS 窓周期に間引く）
        // 間引いた合間はアタック/リリース IIR が前回ターゲットへ追従するため、
        // 平滑時定数（20〜250ms）に対し 10ms の階段更新は聴感上知覚されない
        if (++m_frameCounter >= m_gainRecalcInterval) {
            m_frameCounter = 0;
            const float rmsLevel = std::sqrt(std::max(m_rmsState, 1e-12f));
            const float levelDb  = 20.0f * std::log10(rmsLevel);
            const float comprDb  = (levelDb > kThresholdDb)
                                 ? -(levelDb - kThresholdDb) * (1.0f - 1.0f / kRatio)
                                 : 0.0f;
            m_targetGain = std::pow(10.0f, (comprDb + kMakeupDb) / 20.0f);
        }

        // アタック/リリース平滑（ゲイン低下方向はアタック、回復方向はリリース）
        const float coeff = (m_targetGain < m_currentGain) ? m_attackCoeff : m_releaseCoeff;
        m_currentGain = coeff * m_currentGain + (1.0f - coeff) * m_targetGain;

        // バイパスランプ更新（ON→OFF / OFF→ON を 50ms かけて線形補間する）
        if (m_enabled) {
            m_applyRatio = std::min(1.0f, m_applyRatio + m_rampStep);
        }
        else {
            m_applyRatio = std::max(0.0f, m_applyRatio - m_rampStep);
        }

        // 各チャンネルに適用: DSP 出力とバイパスをランプ比率で補間しリミッタで上限保証
        // 補間後にもリミッタを掛けることで raw が ±1.0 超の入力でもピーク保証を維持する
        for (int ch = 0; ch < m_channels; ++ch) {
            const float raw = frame[ch];
            float processed = raw * m_currentGain;
            processed = std::clamp(processed, -kLimiterCeil, kLimiterCeil);
            const float blended = m_applyRatio * processed + (1.0f - m_applyRatio) * raw;
            frame[ch] = std::clamp(blended, -kLimiterCeil, kLimiterCeil);
        }
    }
}
