#include "Normalizer.h"
#include <cassert>
#include <cmath>
#include <algorithm>

// アップワード圧縮のレシオ（閾値との差のうち持ち上げる割合）
// 6:1 で「1 - 1/6 = 83% の差を埋める」。threshold との gap が大きいほどブーストが増え、
// makeup で上限キャップされる。発話の quietness に応じた滑らかな持ち上げを得るための典型値
static constexpr float kRatio       = 6.0f;
// リミッタの絶対上限（線形振幅、1.0=フルスケール）
// 0.97 は -0.26 dBFS。後段の resampler overshoot（最大 ~2%）を見込んでも 0.99 で頭打ちになる安全係数
static constexpr float kLimiterCeil = 0.97f;
// ソフトリミッタのニー（線形通過の上限、線形振幅）
// この値未満は無加工で素通しし、kLimiterCeil との間を tanh で滑らかに飽和させる。
// 0.90 ≒ -0.9 dBFS。アップワード圧縮では大音量を素通しするため通常時は不発で、
// フルスケール直前のピークと後段 resampler overshoot のみをこの曲線で穏やかに頭打ちする
// 安全網として機能する。値を下げるほど早めに飽和が効くが、素通し帯域が狭まり大音量が鈍る
static constexpr float kLimiterKnee = 0.90f;
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

// ソフトリミッタ
// 振幅が kLimiterKnee 未満なら無加工で素通しし、kLimiterKnee 〜 kLimiterCeil を
// tanh で滑らかに飽和させて出力が kLimiterCeil を超えないよう保証する。
// ニーで値・傾きとも連続（C1）になり、ハードクリップの角を取って歪みを和らげる
static float softLimit(float x)
{
    const float a = std::fabs(x);
    if (a <= kLimiterKnee) {
        return x;
    }
    const float over   = a - kLimiterKnee;
    const float range  = kLimiterCeil - kLimiterKnee;
    const float shaped = kLimiterKnee + range * std::tanh(over / range);
    return std::copysign(shaped, x);
}

// 閾値 thresholdDb 相当の振幅を二乗した RMS 追跡器の初期値を返す
// 初回フレームで levelDb が -∞ に振れる事態を防ぎ、初回 recalc 時点で
// 真の RMS への収束をより速く始められるよう、ありえそうな信号レベル付近に
// プリセットしておく
static float initialRmsState(float thresholdDb)
{
    const float amp = std::pow(10.0f, thresholdDb / 20.0f);
    return amp * amp;
}

Normalizer::Normalizer(int sampleRate,
                       int channels,
                       Level initialLevel,
                       const LevelParams& small,
                       const LevelParams& medium,
                       const LevelParams& large)
    : m_channels(std::max(1, channels))
    , m_level(initialLevel)
    , m_applyRatio(initialLevel == Level::Off ? 0.0f : 1.0f)
    , m_currentGain(1.0f)
    , m_targetGain(1.0f)
{
    // 強度別パラメータ表を構築する。Off は未使用だが Medium 値を入れて未初期化アクセスを防ぐ
    m_levelParams[static_cast<int>(Level::Off)]    = medium;
    m_levelParams[static_cast<int>(Level::Small)]  = small;
    m_levelParams[static_cast<int>(Level::Medium)] = medium;
    m_levelParams[static_cast<int>(Level::Large)]  = large;

    // 初期パラメータ：Off の場合は Medium 相当を仮置きする
    // process は Off + applyRatio==0 なら早期 return するため、Off 状態でパラメータが使われることはない。
    // Off → 他強度への遷移時に setLevel 経由で再設定される
    const Level paramSrc = (initialLevel == Level::Off) ? Level::Medium : initialLevel;
    m_thresholdDb = m_levelParams[static_cast<int>(paramSrc)].thresholdDb;
    m_makeupDb    = m_levelParams[static_cast<int>(paramSrc)].makeupDb;
    m_rmsState    = initialRmsState(m_thresholdDb);

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
    // currentGain=1.0 → makeup 適用後ゲインへ kReleaseMs かけて緩慢に立ち上がるため
    // シーク直後にポップノイズとして体感される
    m_frameCounter = 0;
}

void Normalizer::setLevel(Level level)
{
    if (level == m_level) return;

    // Off 以外の場合のみ threshold / makeup を差し替える
    // Off → ON 遷移時は新強度のパラメータで圧縮が始まり、applyRatio ランプで raw → processed へ滑らかに移行する。
    // ON → Off 遷移時はパラメータを維持したまま applyRatio が 0 へ収束し、その後 process は早期 return する
    if (level != Level::Off) {
        // Off は呼ばれない前提だが上の if でガード済み
        assert(level == Level::Small || level == Level::Medium || level == Level::Large);
        m_thresholdDb = m_levelParams[static_cast<int>(level)].thresholdDb;
        m_makeupDb    = m_levelParams[static_cast<int>(level)].makeupDb;

        // Off → ON 遷移時：Off 中に凍結していたゲイン状態をリセットして再 warmup する。
        // Off 中は早期 return でスキップするため targetGain が古い値で凍結しており、
        // そのまま applyRatio ランプを立ち上げると旧ゲインが 50ms 混入する。
        // reset() と同等の初期化で currentGain=1.0 → 正規値へ kReleaseMs かけて収束させる
        if (m_level == Level::Off) {
            m_rmsState    = initialRmsState(m_thresholdDb);
            m_currentGain = 1.0f;
            m_targetGain  = 1.0f;
            m_frameCounter = 0;
        }
    }
    m_level = level;
}

void Normalizer::reset()
{
    // シーク後に古いコンプレッサ状態が残らないようリセットする。
    // 再生再開直後は kRmsWindowMs（10ms）相当の warmup として currentGain=1.0 を据え置き、
    // 経過後に初回 recalc で targetGain を確定して以降 kAttackMs/kReleaseMs で追従する
    m_rmsState     = initialRmsState(m_thresholdDb);
    m_currentGain  = 1.0f;
    m_targetGain   = 1.0f;
    // reset 後 1 窓分（kRmsWindowMs）は再計算を待ち、その間 currentGain は 1.0 のまま据え置く。
    // 即時再計算するとシーク直後にポップノイズが乗る（コンストラクタの該当コメント参照）
    m_frameCounter = 0;
}

void Normalizer::process(float* samples, std::ptrdiff_t n)
{
    const std::ptrdiff_t frames = n / m_channels;

    // 完全バイパス：Off かつランプ収束済み（m_applyRatio == 0）の場合は DSP 計算ごとスキップする。
    // 出力は raw 入力そのままで in-place 不変。次回 ON 時はパラメータ再設定 + applyRatio が 0→1 へ
    // 50ms かけて遷移する間に過渡応答も raw 寄りでマスクされ聴感影響は出ない
    if (m_level == Level::Off && m_applyRatio == 0.0f) {
        return;
    }

    const bool enabled = (m_level != Level::Off);
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
            // 閾値未満のみ持ち上げる（アップワード圧縮）。閾値以上はゲイン 0dB で完全素通し。
            // ブースト量は閾値との差を 1/kRatio だけ詰めた値とし、makeup を上限にキャップする
            float boostDb = (levelDb < m_thresholdDb)
                          ? (m_thresholdDb - levelDb) * (1.0f - 1.0f / kRatio)
                          : 0.0f;
            boostDb = std::min(boostDb, m_makeupDb);
            m_targetGain = std::pow(10.0f, boostDb / 20.0f);
        }

        // アタック/リリース平滑（ゲイン低下方向はアタック、回復方向はリリース）
        const float coeff = (m_targetGain < m_currentGain) ? m_attackCoeff : m_releaseCoeff;
        m_currentGain = coeff * m_currentGain + (1.0f - coeff) * m_targetGain;

        // バイパスランプ更新（ON→1.0, Off→0.0 へ線形補間）
        if (enabled) {
            m_applyRatio = std::min(1.0f, m_applyRatio + m_rampStep);
        }
        else {
            m_applyRatio = std::max(0.0f, m_applyRatio - m_rampStep);
        }

        // 各チャンネルに適用: DSP 出力とバイパスをランプ比率で補間しソフトリミッタで上限保証
        // 補間後にソフトリミッタを掛けることで raw が ±1.0 超の入力でもピーク保証を維持する。
        // 補間前の中間クリップは行わず、最終出力 1 回だけ softLimit に通して二重整形を避ける
        for (int ch = 0; ch < m_channels; ++ch) {
            const float raw      = frame[ch];
            const float processed = raw * m_currentGain;
            const float blended   = m_applyRatio * processed + (1.0f - m_applyRatio) * raw;
            frame[ch] = softLimit(blended);
        }
    }
}
