#ifndef SPEECHENHANCER_H
#define SPEECHENHANCER_H

#include <QtGlobal>
#include <memory>

// 音声強調 DSP（WebRTC Audio Processing ラッパ）
// AGC2（自動ゲイン制御）+ ノイズ抑制 + ハイパスフィルタで会議音声のレベルを均す。
// SoundTouch と同じ put / receive ストリーミング interface を提供し、AudioWorker の流量制御へ自然に接続する。
class SpeechEnhancer
{
public:
    // 強度レベル
    // Off は APM を通さず素通し（ステレオ保持）。Low / Medium / High で NS / AGC を段階的に強める。
    enum class Level {
        Off = 0,
        Low = 1,
        Medium = 2,
        High = 3
    };

    // レベル別 DSP パラメータ
    // avply.toml の [speech_enhance] から構築する。
    struct LevelParams {
        int noiseSuppressionLevel; // 0=Low, 1=Moderate, 2=High, 3=VeryHigh
        float fixedGainDb;         // AGC2 fixed_digital.gain_db（固定ブースト）
        float maxGainDb;           // AGC2 adaptive_digital.max_gain_db（適応ブースト上限）
    };

    // コンストラクタ
    // sampleRate / channels は入出力 interleaved フォーマット。APM 実体は audio thread での生成を前提とする。
    SpeechEnhancer(int sampleRate, int channels,
                   const LevelParams& low,
                   const LevelParams& medium,
                   const LevelParams& high,
                   Level initialLevel);
    ~SpeechEnhancer();

    SpeechEnhancer(const SpeechEnhancer&) = delete;
    SpeechEnhancer& operator=(const SpeechEnhancer&) = delete;

    // レベル変更
    // ApplyConfig を内部で呼ぶため ProcessStream と同一スレッド（audio thread）からのみ呼ぶ。
    void setLevel(Level level);
    Level level() const;

    // interleaved サンプル投入
    // frames は 1ch あたりのフレーム数。内部で 10ms 単位（48kHz で 480 フレーム）に蓄積して APM 処理する。
    void pushInterleaved(const float* in, qsizetype frames);

    // 処理済み interleaved サンプル取り出し
    // 戻り値は実際に取り出した 1ch あたりのフレーム数。
    qsizetype pullInterleaved(float* out, qsizetype maxFrames);

    // 取り出し可能フレーム数
    qsizetype availableFrames() const;

    // 内部状態クリア
    // シーク・ファイル切替時に APM を Initialize し蓄積 / 出力バッファを破棄する。
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // SPEECHENHANCER_H
