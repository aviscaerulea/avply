#include "AudioWorker.h"
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <SoundTouch.h>
#include <algorithm>
#include <cmath>
#include <cstring>
// SSE 制御レジスタ操作（denormal flush 設定用）
#include <xmmintrin.h>
#include <pmmintrin.h>

AudioWorker::AudioWorker(const QAudioFormat& format,
                         int  initialNormalizeLevel,
                         int  initialVoiceClarityLevel,
                         const Normalizer::LevelParams& normalizerSmall,
                         const Normalizer::LevelParams& normalizerMedium,
                         const Normalizer::LevelParams& normalizerLarge,
                         const VoiceClarity::LevelParams& voiceClaritySmall,
                         const VoiceClarity::LevelParams& voiceClarityMedium,
                         const VoiceClarity::LevelParams& voiceClarityLarge,
                         QObject* parent)
    : QObject(parent)
    , m_format(format)
    , m_normalizer(format.sampleRate(), format.channelCount(),
                   static_cast<Normalizer::Level>(
                       std::clamp(initialNormalizeLevel,
                                  static_cast<int>(Normalizer::Level::Off),
                                  static_cast<int>(Normalizer::Level::Large))),
                   normalizerSmall, normalizerMedium, normalizerLarge)
    , m_voiceClarity(format.sampleRate(), format.channelCount(),
                     static_cast<VoiceClarity::Level>(
                         std::clamp(initialVoiceClarityLevel,
                                    static_cast<int>(VoiceClarity::Level::Off),
                                    static_cast<int>(VoiceClarity::Level::Large))),
                     voiceClaritySmall, voiceClarityMedium, voiceClarityLarge)
{
}

AudioWorker::~AudioWorker() = default;

void AudioWorker::start()
{
    // audio thread の SSE 制御レジスタで denormal flush を有効化する。
    // Normalizer / VoiceClarity の IIR は無音区間で内部状態が指数的に減衰し、
    // 数秒で denormal float（1.18e-38 未満）に到達する。x86 では denormal 演算が
    // 通常の 50〜100 倍遅く、audio thread 上で発生すると sink underrun の引き金になる。
    // FTZ/DAZ は MXCSR のスレッドローカルフラグのため、必ず audio thread 側で設定する
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    // 所属スレッド（audio thread）で QAudioSink を生成して start する。
    // QAudioSink / QIODevice の thread affinity を所属スレッドで一貫させるため、
    // コンストラクタ側では生成せず必ず本スロット経由で生成する
    m_sink = new QAudioSink(m_format, this);

    // 内部バッファを 200ms 相当に拡張する（48kHz stereo Float の場合 76800 バイト）。
    // デフォルトの WASAPI バッファは数 ms と極小で、1.5x 等の高速再生時にデコーダから
    // バーストで届くサンプルを吸収しきれず write が partial になり音が欠ける。
    // setBufferSize は start() より前に呼ぶ必要がある
    const qsizetype bytesPerSec =
        static_cast<qsizetype>(m_format.bytesForDuration(1000 * 1000));
    m_sink->setBufferSize(bytesPerSec / 5);

    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed:" << m_sink->error();
    }
    // 起動時情報は qDebug 経由とする。HighPriority audio thread からの周期 qInfo は
    // OutputDebugString 同期 I/O で数 ms ブロックし sink underrun の引き金になり得るため、
    // audio thread からのログは原則 Debug 出力に抑える
    qDebug() << "AudioWorker: QAudioSink bufferSize="
             << m_sink->bufferSize()
             << "bytes (requested" << (bytesPerSec / 5) << ")";

    // SoundTouch を所属スレッドで生成する。
    // QAudioBufferOutput が pitchCompensation を無視するため、AudioWorker 側で
    // playback rate に応じた時間圧縮 / 伸長を行う。setTempo は onAudioBuffer 冒頭で
    // m_pendingRate を読み取って適用する（GUI thread から DirectConnection で呼ばれても安全にするため）
    m_stretch = std::make_unique<soundtouch::SoundTouch>();
    m_stretch->setSampleRate(static_cast<uint>(m_format.sampleRate()));
    m_stretch->setChannels(static_cast<uint>(m_format.channelCount()));

    // WSOLA パラメータを音声（speech）向けに固定する
    // SoundTouch の既定は音楽向けの自動設定で、会議音声の 1.2 倍程度ではシーケンスの
    // つなぎ目に微小なサンプル不連続・局所ピークが出る。後段 Normalizer の makeup gain が
    // これを増幅し、可聴なプチノイズ化していた。公式 README（TDStretch.h）が speech 用途に
    // 推奨する固定値（SEQUENCE 40 / SEEKWINDOW 15 / OVERLAP 8 ms）に切り替えてつなぎ目の
    // アーティファクト自体を抑える。AA フィルタは既定（有効）のまま品質を優先する
    m_stretch->setSetting(SETTING_SEQUENCE_MS,   40);
    m_stretch->setSetting(SETTING_SEEKWINDOW_MS, 15);
    m_stretch->setSetting(SETTING_OVERLAP_MS,     8);

    const double initialRate = m_pendingRate.load(std::memory_order_relaxed);
    m_stretch->setTempo(initialRate);
    m_appliedRate = initialRate;
}

void AudioWorker::onAudioBuffer(const QAudioBuffer& buf)
{
    if (!m_sinkDev || !m_stretch || !m_sink) return;

    // GUI thread から要求された再生速度を audio thread 上で SoundTouch に反映する。
    // setTempo はスレッド安全ではないため、必ずこの経路（onAudioBuffer = audio thread）でのみ呼ぶ。
    // decoder の rate 変更が反映されたバッファが届く前に tempo を合わせることで、
    // 旧 tempo 前提で受け取った入力が SoundTouch 内に滞留し partial write の引き金になるのを防ぐ
    const double pendingRate = m_pendingRate.load(std::memory_order_relaxed);
    if (std::abs(pendingRate - m_appliedRate) > 1e-9) {
        m_stretch->setTempo(pendingRate);
        m_appliedRate = pendingRate;
    }

    // 等速再生（rate≒1.0）は SoundTouch をバイパスする。
    // tempo 1.0 でも SoundTouch は WSOLA のオーバーラップ加算でつなぎ目に微小な不連続を残し、
    // 後段 Normalizer の makeup gain がそれを可聴なプチノイズへ増幅するため、等速では経路ごと外す。
    // バイパスの ON/OFF が切り替わる瞬間に SoundTouch 内の旧 tempo 残量を破棄する。
    // bypass へ入る場合は以後 putSamples しないため残量が宙に浮き、bypass から出る場合も
    // 残量は旧経路のもので不要。速度変更の瞬間に数十 ms 欠落するが体感されない
    const bool bypass = std::abs(pendingRate - 1.0) < 1e-6;
    if (bypass != m_bypassActive) {
        m_stretch->clear();
        m_bypassActive = bypass;
    }

    // 初回バッファのフォーマット記録
    // format mismatch 検出用の診断ログ。m_firstBufferReported は reset() で false に戻すため、
    // ファイル切替・シーク後の最初のバッファでも再出力される
    if (!m_firstBufferReported) {
        m_firstBufferReported = true;
        const auto& f = buf.format();
        qDebug() << "AudioWorker: first buffer format:"
                 << "rate=" << f.sampleRate()
                 << "ch=" << f.channelCount()
                 << "fmt=" << static_cast<int>(f.sampleFormat())
                 << "frames=" << buf.frameCount()
                 << "bytes=" << buf.byteCount();
    }

    if (buf.format().sampleFormat() != QAudioFormat::Float) return;

    const int channels = m_format.channelCount();
    const qsizetype inFrames = buf.frameCount();
    if (inFrames <= 0) return;

    // フェールセーフ：連続 underrun 時の蓄積暴走を抑止する
    // sink への書き戻し失敗が続くと SoundTouch 内出力キュー (numSamples) と m_pendingTail が
    // 入力レート分だけ際限なく膨らみ、再生レイテンシが秒単位で増大する。
    // 約 2 秒相当を超えた時点で両方破棄して即時回復させる。聴感上は一瞬の音飛びになるが
    // 持続的なレイテンシ膨張に比べ復帰コストが圧倒的に低い。
    // チェックは putSamples の前に行う。後置きにすると当該バッファ投入分が即捨てされ、
    // overflow 復旧後の最初の出力が空となって音切れが連鎖する
    constexpr qint64 kOverflowSeconds = 2;
    const qint64 sampleRate    = m_format.sampleRate();
    const qint64 bytesPerSec   = static_cast<qint64>(m_format.bytesForDuration(1000 * 1000));
    const qint64 maxStretchFrames = sampleRate  * kOverflowSeconds;
    const qint64 maxTailBytes     = bytesPerSec * kOverflowSeconds;
    const qint64 stretchFrames    = static_cast<qint64>(m_stretch->numSamples());
    if (stretchFrames > maxStretchFrames || m_pendingTail.size() > maxTailBytes) {
        qWarning() << "AudioWorker: overflow guard triggered."
                   << "stretchFrames=" << stretchFrames
                   << "pendingTail=" << m_pendingTail.size()
                   << "— clearing both to recover";
        m_stretch->clear();
        m_pendingTail.clear();
        // 統計に underrun として 1 件計上して 1 秒ログから可視化する
        ++m_statsUnderruns;
        // 異常時はフォーマット診断ログを再出力する。
        // overflow が連発する局面こそ「直前まで何を受けていたか」の記録が重要なため、
        // 次バッファで first-buffer 経路の qDebug を再走させる
        m_firstBufferReported = false;
    }

    // 統計集計用ローカル変数
    qint64 totalInBytes  = 0;
    qint64 totalOutBytes = 0;
    int    underruns     = 0;
    int    writes        = 0;

    if (bypass) {
        // 等速再生：SoundTouch を通さず raw 入力を直接 DSP へ通す。
        // DSP 適用済みサンプルを m_pendingTail 末尾へ積み、後続の flush ブロックで
        // 既存残量と同じ経路・順序で sink へ書き出す。sink フル時も pendingTail に
        // 滞留して順序とサンプルが保たれるため、SoundTouch のような滞留先が無くても欠落しない
        const qsizetype outSamples = inFrames * channels;
        const qsizetype outBytes   = outSamples * static_cast<qsizetype>(sizeof(float));
        if (m_workBuf.size() < outBytes) {
            m_workBuf.resize(outBytes);
        }
        std::memcpy(m_workBuf.data(), buf.constData<float>(), static_cast<size_t>(outBytes));
        float* data = reinterpret_cast<float*>(m_workBuf.data());
        m_voiceClarity.process(data, outSamples);
        m_normalizer.process(data, outSamples);
        m_pendingTail.append(m_workBuf.constData(), outBytes);
    }
    else {
        // SoundTouch に入力サンプル（interleaved float）を投入する
        // 出力は tempo に応じて入力 frame 数 / tempo 程度のフレーム数になる。
        // overflow 復旧後も同経路で当該バッファ分を投入する（捨てると復旧冒頭で空出力になる）
        m_stretch->putSamples(buf.constData<float>(), static_cast<uint>(inFrames));
    }

    // 音量適用のラムダ
    // m_pendingTail / receiveSamples 出力のいずれも pre-volume（post-normalizer）として保持し、
    // sink への書き込み直前にここで最新 m_volume を適用する。退避から書き戻しの間に
    // ユーザが音量を変更しても旧音量で出力されない
    auto applyVolume = [this](const char* src, qint64 bytes) -> const char* {
        if (m_volumeWork.size() < bytes) {
            m_volumeWork.resize(bytes);
        }
        const float     vol     = static_cast<float>(m_volume);
        const float*    in      = reinterpret_cast<const float*>(src);
        float*          out     = reinterpret_cast<float*>(m_volumeWork.data());
        const qsizetype samples = bytes / static_cast<qsizetype>(sizeof(float));
        for (qsizetype i = 0; i < samples; ++i) {
            out[i] = in[i] * vol;
        }
        return m_volumeWork.constData();
    };

    // sink の partial write 残量を最優先で flush する。
    // 前回 onAudioBuffer で bytesFree() 不足により書ききれなかった末尾サンプルを保持しており、
    // ここで書き戻すことでサンプル不連続点（プチノイズ）の発生を防ぐ
    if (!m_pendingTail.isEmpty()) {
        const qint64 free = m_sink->bytesFree();
        if (free <= 0) {
            // sink が満タンなので receiveSamples せず次回まで待つ（SoundTouch 側で滞留させる）
            return;
        }
        const qint64 toWrite = std::min<qint64>(free, m_pendingTail.size());
        const char*  outPtr  = applyVolume(m_pendingTail.constData(), toWrite);
        const qint64 written = m_sinkDev->write(outPtr, toWrite);
        totalInBytes  += toWrite;
        totalOutBytes += (written > 0 ? written : 0);
        ++writes;
        const qint64 consumed = (written > 0) ? written : 0;
        if (consumed < m_pendingTail.size()) {
            // 一部しか書けなかった分を先頭から除去して保持し、receiveSamples は次回まで持ち越す
            // pendingTail は pre-volume のまま remove するので、次回も最新音量が適用される
            m_pendingTail.remove(0, consumed);
            ++underruns;
            return;
        }
        m_pendingTail.clear();
    }

    // SoundTouch から time-stretched サンプルを取り出して DSP・音量・sink への書き込みを行う
    // 1 回の receiveSamples で全部出ない場合があるため received == 0 までループする
    // バイパス時は SoundTouch を経由しないため本ループ全体をスキップする（出力は上の pendingTail 経路で完結）
    if (!bypass) {
    // receiveSamples 1 回あたりの取り出し上限フレーム数
    // 4096 ≒ 85ms@48kHz。大きすぎると 1 ループが長くなり sink bytesFree チェック粒度が落ちる
    constexpr uint kRecvBatchFrames = 4096;
    const qsizetype batchBytes =
        static_cast<qsizetype>(kRecvBatchFrames) * channels * static_cast<qsizetype>(sizeof(float));
    if (m_workBuf.size() < batchBytes) {
        m_workBuf.resize(batchBytes);
    }
    float* recv = reinterpret_cast<float*>(m_workBuf.data());

    for (;;) {
        // sink の空きを見て書ける見込みがなければ receiveSamples せず終了する
        // （SoundTouch 内に蓄積させ、次回 onAudioBuffer で消化する。
        // 1.20 倍など消費レート＜流入レートの局面では蓄積が逆圧力として機能する）
        const qint64 free = m_sink->bytesFree();
        if (free <= 0) break;

        const uint received = m_stretch->receiveSamples(recv, kRecvBatchFrames);
        if (received == 0) break;

        const qsizetype outSamples = static_cast<qsizetype>(received) * channels;
        const qsizetype outBytes   = outSamples * static_cast<qsizetype>(sizeof(float));

        // 音声明瞭化 → ノーマライズの順で処理する。
        // EQ のピーキングブーストで振幅が拡張されても後段 Normalizer のリミッタ（±0.97）で
        // 自然に上限を保証できる。順序を逆にすると EQ が limited 信号を再ブーストして clip するリスクがある
        m_voiceClarity.process(recv, outSamples);
        m_normalizer.process(recv, outSamples);

        // sink の空きに収まる分のみ即時書き込み、残量は pre-volume のまま退避する。
        // m_workBuf は pre-volume（post-normalizer）状態のままで、音量は applyVolume 内でコピー適用する
        const qint64 toWrite = std::min<qint64>(free, outBytes);
        const char*  outPtr  = applyVolume(m_workBuf.constData(), toWrite);
        const qint64 written = m_sinkDev->write(outPtr, toWrite);
        totalInBytes  += toWrite;
        totalOutBytes += (written > 0 ? written : 0);
        ++writes;
        const qint64 consumed = (written > 0) ? written : 0;

        // toWrite を超える残量は pre-volume のまま pendingTail に退避する
        if (toWrite < outBytes) {
            const char*     tailSrc   = m_workBuf.constData() + toWrite;
            const qsizetype tailBytes = outBytes - toWrite;
            m_pendingTail.append(tailSrc, tailBytes);
            ++underruns;
            break;
        }
        // bytesFree() 制約下で write < toWrite はまれ。さらに不足した分も pre-volume で退避する
        if (consumed < toWrite) {
            const char*     tailSrc   = m_workBuf.constData() + consumed;
            const qsizetype tailBytes = toWrite - consumed;
            m_pendingTail.append(tailSrc, tailBytes);
            ++underruns;
            break;
        }
    }
    } // if (!bypass)

    // 診断ログ：1 秒ごとに集計（毎呼び出し underrun を出すと音飛びの体感悪化に繋がるため）。
    // 周期 qInfo は OutputDebugString 同期 I/O で audio thread を数 ms ブロックするため
    // qDebug に格下げし、リリースビルドでは QT_NO_DEBUG_OUTPUT で完全抑止できる位置付けにする。
    // 集計変数はメンバ化し reset() でリセットすることで、シーク直後の集計区間が不正な長さに
    // ならないようにする
    m_statsInBytes   += totalInBytes;
    m_statsOutBytes  += totalOutBytes;
    m_statsWrites    += writes;
    m_statsUnderruns += underruns;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_statsWinStart == 0) m_statsWinStart = now;
    if (now - m_statsWinStart >= 1000) {
        qDebug() << "AudioWorker: 1s stats"
                 << "in=" << (m_statsInBytes / 1024) << "KB"
                 << "out=" << (m_statsOutBytes / 1024) << "KB"
                 << "writes=" << m_statsWrites
                 << "underruns=" << m_statsUnderruns
                 << "pendingTail=" << m_pendingTail.size()
                 << "bytesFree=" << m_sink->bytesFree()
                 << "tempo=" << m_stretch->getInputOutputSampleRatio();
        m_statsWinStart  = now;
        m_statsInBytes   = 0;
        m_statsOutBytes  = 0;
        m_statsWrites    = 0;
        m_statsUnderruns = 0;
    }
}

void AudioWorker::reset()
{
    // シーク時の sink 積み残し破棄と DSP 状態リセット。
    // 50ms 以内の連打ではスロットリング側に流し、sink stop()→start() を間引く
    m_normalizer.reset();
    m_voiceClarity.reset();
    if (m_stretch) m_stretch->clear();
    // sink を捨てるため partial write 残量も破棄する（古いサンプルを再開後に書き出さない）。
    // m_workBuf / m_volumeWork はシーク連打中の audio thread malloc/free スパイクを避けるため
    // ここでは解放せず保持する（解放は teardown / forceReset でのみ行う）
    m_pendingTail.clear();
    // 1 秒集計ウィンドウもリセットする。リセット直後の集計区間が 1 秒超 / 未満で出ないようにする
    m_statsWinStart  = 0;
    m_statsInBytes   = 0;
    m_statsOutBytes  = 0;
    m_statsWrites    = 0;
    m_statsUnderruns = 0;
    m_firstBufferReported = false;
    if (!m_sink) return;
    // シーク連打スロットリング
    // 直近 50ms 以内の再 reset では sink stop()→start() をスキップする。WASAPI 完全再起動の
    // 約 30ms ブロックが audio thread に蓄積するのを防ぐ。スロットリング窓は「最後に実際に
    // restart した時刻」から 50ms とし、throttle 側 return では m_lastSinkRestartMs を
    // 更新しない。これにより 49ms 間隔の連打中でも 50ms ごとに一度は sink restart が走り、
    // WASAPI バッファに残った旧サンプルが定期的に flush される
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastSinkRestartMs < 50) {
        return;
    }
    m_lastSinkRestartMs = now;
    m_sink->stop();
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed (after reset):" << m_sink->error();
    }
}

void AudioWorker::forceReset()
{
    // ソース切替時の強制リセット。throttle を無視して必ず sink を stop→start し、
    // 前ソースのサンプルが WASAPI バッファに残留することを防ぐ
    m_normalizer.reset();
    m_voiceClarity.reset();
    if (m_stretch) m_stretch->clear();
    m_workBuf.clear();
    m_volumeWork.clear();
    m_pendingTail.clear();
    m_statsWinStart  = 0;
    m_statsInBytes   = 0;
    m_statsOutBytes  = 0;
    m_statsWrites    = 0;
    m_statsUnderruns = 0;
    m_firstBufferReported = false;
    if (!m_sink) return;
    m_lastSinkRestartMs = QDateTime::currentMSecsSinceEpoch();
    m_sink->stop();
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed (after forceReset):" << m_sink->error();
    }
}

void AudioWorker::setVolume(double volume)
{
    m_volume = volume;
}

void AudioWorker::setNormalizeLevel(int level)
{
    const int clamped = std::clamp(level,
                                   static_cast<int>(Normalizer::Level::Off),
                                   static_cast<int>(Normalizer::Level::Large));
    m_normalizer.setLevel(static_cast<Normalizer::Level>(clamped));
}

void AudioWorker::setVoiceClarityLevel(int level)
{
    const int clamped = std::clamp(level,
                                   static_cast<int>(VoiceClarity::Level::Off),
                                   static_cast<int>(VoiceClarity::Level::Large));
    m_voiceClarity.setLevel(static_cast<VoiceClarity::Level>(clamped));
}

void AudioWorker::setPlaybackRate(double rate)
{
    // 再生速度を atomic に受け取り、実際の SoundTouch::setTempo は
    // onAudioBuffer 冒頭（audio thread 上）で適用する。
    // この slot は VideoView から DirectConnection で GUI thread から直接呼ばれる場合があり、
    // SoundTouch がスレッド安全でないため、ここでは触らない。
    // tempo=1.5 → 入力 1.5 秒分を出力 1 秒分に時間圧縮（ピッチ保持）
    // 範囲外 / 不正値は無視する（0 や負値は SoundTouch の前提を破る）
    if (rate <= 0.0) return;
    m_pendingRate.store(rate, std::memory_order_relaxed);
}

void AudioWorker::teardown()
{
    // audio thread 上で QAudioSink・SoundTouch を安全に破棄する。
    // QAudioSink は audio thread で生成しているため GUI thread からの delete は thread affinity 違反になる
    if (m_sink) {
        m_sink->stop();
        m_sink = nullptr;
        m_sinkDev = nullptr;
    }
    m_stretch.reset();
}
