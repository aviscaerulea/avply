#include "AudioWorker.h"
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <SoundTouch.h>
#include <algorithm>
#include <cmath>

AudioWorker::AudioWorker(const QAudioFormat& format,
                         bool initialNormalize,
                         bool initialVoiceClarity,
                         QObject* parent)
    : QObject(parent)
    , m_format(format)
    , m_normalizer(format.sampleRate(), format.channelCount(), initialNormalize)
    , m_voiceClarity(format.sampleRate(), format.channelCount(), initialVoiceClarity)
{
}

AudioWorker::~AudioWorker() = default;

void AudioWorker::start()
{
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
    // 持続的なレイテンシ膨張に比べ復帰コストが圧倒的に低い
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
    }

    // SoundTouch に入力サンプル（interleaved float）を投入する
    // 出力は tempo に応じて入力 frame 数 / tempo 程度のフレーム数になる
    m_stretch->putSamples(buf.constData<float>(), static_cast<uint>(inFrames));

    // 統計集計用ローカル変数
    qint64 totalInBytes  = 0;
    qint64 totalOutBytes = 0;
    int    underruns     = 0;
    int    writes        = 0;

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
    // ソース切替・シーク時に sink のバッファを破棄して Normalizer・SoundTouch 状態もリセットする。
    // QAudioSink::reset() のみでは停止状態への遷移が保証されないため
    // stop() → start() で状態機械を確実にリセットする
    m_normalizer.reset();
    m_voiceClarity.reset();
    if (m_stretch) m_stretch->clear();
    // ソース切替時にバッファを解放する（次の onAudioBuffer で必要サイズに再確保される）
    m_workBuf.clear();
    m_volumeWork.clear();
    // sink を捨てるため partial write 残量も破棄する（古いサンプルを再開後に書き出さない）
    m_pendingTail.clear();
    // 1 秒集計ウィンドウもリセットする。リセット直後の集計区間が 1 秒超 / 未満で出ないようにする
    m_statsWinStart  = 0;
    m_statsInBytes   = 0;
    m_statsOutBytes  = 0;
    m_statsWrites    = 0;
    m_statsUnderruns = 0;
    m_firstBufferReported = false;
    if (!m_sink) return;
    m_sink->stop();
    m_sinkDev = m_sink->start();
    if (!m_sinkDev) {
        qWarning() << "AudioWorker: QAudioSink::start() failed (after reset):" << m_sink->error();
    }
}

void AudioWorker::setVolume(double volume)
{
    m_volume = volume;
}

void AudioWorker::setNormalizeEnabled(bool enabled)
{
    m_normalizer.setEnabled(enabled);
}

void AudioWorker::setVoiceClarityEnabled(bool enabled)
{
    m_voiceClarity.setEnabled(enabled);
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
