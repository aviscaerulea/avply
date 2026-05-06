#pragma once
#include <QObject>
#include <QAudioFormat>
#include <QAudioBuffer>

class QAudioSink;
class QIODevice;

// 専用スレッドで QAudioSink への書き込みを担うワーカ
// audioBufferReceived は decoder thread で発火するが、これを GUI thread 経由ではなく
// 本ワーカの専用スレッドに QueuedConnection で配送することで、
// modal size/move loop による GUI thread のブロックから音声経路を独立させる。
// QAudioSink/QIODevice の thread affinity は所属スレッドで一貫させる必要があるため、
// sink の生成・破棄・書き込みはすべて本ワーカのスロット経由で行う
class AudioWorker : public QObject {
    Q_OBJECT
public:
    // QAudioFormat だけを保持し、sink の実体生成は start() 経由で所属スレッドに行わせる
    explicit AudioWorker(const QAudioFormat& format, QObject* parent = nullptr);
    ~AudioWorker() override;

public slots:
    // 所属スレッドで QAudioSink を生成・起動する
    // moveToThread 後に QMetaObject::invokeMethod 等で QueuedConnection 呼び出しすること
    void start();

    // 受信したオーディオバッファに gain を適用して sink に書き込む
    // Float サンプルを tanh でソフトクリップする（hard clip による歪みを避けるため）
    void onAudioBuffer(const QAudioBuffer& buf);

    // ソース切替時の sink 積み残し破棄
    // QAudioSink::reset() は内部バッファのみ破棄するため、必ず stop() してから
    // start() を呼び直して QIODevice を取り直す。stop() を挟まないと積み残し
    // サンプルの再生が続くケースがある
    void reset();

    // 音量ブースト倍率を更新する
    // 書き込みループと同一スレッド（audio thread）からの呼び出しを保証するため、
    // VideoView::setVolumeBoost からは QueuedConnection 経由で呼ぶ
    void setGain(double gain);

    // 所属スレッドで QAudioSink を停止・破棄する
    // QAudioSink は audio thread で生成されており、thread affinity を保つため
    // GUI thread からの破棄ではなく本スロット経由で audio thread 上で解放する。
    // 呼び出し側は QThread::quit() より前に BlockingQueuedConnection で実行すること
    void teardown();

private:
    QAudioFormat m_format;
    QAudioSink*  m_sink    = nullptr;
    QIODevice*   m_sinkDev = nullptr;
    double       m_gain    = 1.0;
};
