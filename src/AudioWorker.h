#pragma once
#include "Normalizer.h"
#include <QObject>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QByteArray>
#include <memory>

class QAudioSink;
class QIODevice;

namespace soundtouch { class SoundTouch; }

// 専用スレッドで Normalizer DSP と QAudioSink への書き込みを担うワーカ
// audioBufferReceived は decoder thread で発火するが、本ワーカの専用スレッドに
// QueuedConnection で配送することで GUI thread のブロックから音声経路を独立させる。
// QAudioSink/QIODevice の thread affinity は所属スレッドで一貫させる必要があるため、
// sink の生成・破棄・書き込みはすべて本ワーカのスロット経由で行う
class AudioWorker : public QObject {
    Q_OBJECT
public:
    // format は QAudioBufferOutput に渡したフォーマットと一致させること
    // initialNormalize で起動時のノーマライズ状態を確定する
    explicit AudioWorker(const QAudioFormat& format, bool initialNormalize,
                         QObject* parent = nullptr);
    ~AudioWorker() override;

public slots:
    // 所属スレッドで QAudioSink を生成・起動する
    // moveToThread 後に QueuedConnection 経由で呼び出すこと
    void start();

    // 受信したバッファに DSP・音量を適用して sink に書き込む
    void onAudioBuffer(const QAudioBuffer& buf);

    // ソース切替・シーク時の sink 積み残し破棄と Normalizer 状態リセット
    void reset();

    // 再生音量を更新する（0.0〜1.0）
    void setVolume(double volume);

    // ノーマライズ ON/OFF を切り替える（50ms ゲインランプで滑らかに遷移する）
    void setNormalizeEnabled(bool enabled);

    // 再生速度を SoundTouch に設定する（音程を保ったまま時間圧縮 / 伸長する）
    // QAudioBufferOutput が pitchCompensation を無視するため AudioWorker 側で時間圧縮する
    void setPlaybackRate(double rate);

    // 所属スレッドで QAudioSink を停止・破棄する
    // QThread::quit() より前に BlockingQueuedConnection で実行すること
    void teardown();

private:
    QAudioFormat m_format;
    Normalizer   m_normalizer;
    QAudioSink*  m_sink    = nullptr;
    QIODevice*   m_sinkDev = nullptr;
    double       m_volume  = 1.0;
    // DSP 処理用の作業バッファ
    // 毎呼び出しの QByteArray 確保は new/delete スパイクを招くため再利用する。
    // 必要サイズに足りないときだけ resize で拡張する
    QByteArray   m_workBuf;
    // SoundTouch インスタンス
    // start() スロットで生成して所属スレッド affinity を確定する
    std::unique_ptr<soundtouch::SoundTouch> m_stretch;
};
