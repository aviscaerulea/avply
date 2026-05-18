#pragma once
#include "Normalizer.h"
#include <QObject>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <QByteArray>
#include <atomic>
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
    // QAudioSink への partial write 残量バッファ（pre-volume / post-normalizer の状態で保持）
    // sink の bytesFree() 不足で書ききれなかった末尾サンプルを保持し、
    // 次回 onAudioBuffer 冒頭で最優先に書き戻す。サンプル欠落（プチノイズ）を防ぐ。
    // 音量は書き込み直前に最新値を適用するため、書き込み失敗から書き戻しの間に
    // ユーザが音量を変更しても旧音量のまま出力されることを避ける
    QByteArray   m_pendingTail;
    // 音量適用用の作業バッファ
    // pre-volume のサンプル列に最新音量を乗じてコピーする一時領域。
    // pendingTail を in-place で書き換えると未書き込み残量が post-volume になり
    // 「次回再書き込み時にさらに音量が変わったら旧値が反映される」連鎖が生じるため別領域で扱う
    QByteArray   m_volumeWork;
    // GUI thread からの再生速度更新を受け取る atomic
    // SoundTouch::setTempo はスレッド安全ではないため、setPlaybackRate slot では本変数だけ更新し、
    // 実際の setTempo は onAudioBuffer 冒頭（audio thread 上）で適用する。
    // decoder の rate 変更で流入レートが変わる前に SoundTouch tempo を合わせることで、
    // SoundTouch 内の蓄積急増による sink underrun を防ぐ
    std::atomic<double> m_pendingRate{1.0};
    // audio thread 上で最後に適用済みの SoundTouch tempo
    // m_pendingRate との差分検知に使う（毎回 setTempo を呼ばない最適化）
    double       m_appliedRate = 1.0;
    // 1 秒集計の診断ログ用カウンタ群
    // function-local static にすると reset() / シーク跨ぎで初期化されず、
    // リセット直後の集計窓が 1 秒超 / 未満となり診断ログの誤読を招くためメンバ化する
    qint64       m_statsWinStart  = 0;
    qint64       m_statsInBytes   = 0;
    qint64       m_statsOutBytes  = 0;
    qint64       m_statsWrites    = 0;
    qint64       m_statsUnderruns = 0;
    // 初回バッファのフォーマット記録フラグ
    // reset() で false に戻し、ファイル切替・シーク後の最初のバッファでも診断ログを再出力する
    bool         m_firstBufferReported = false;
    // SoundTouch インスタンス
    // start() スロットで生成して所属スレッド affinity を確定する
    std::unique_ptr<soundtouch::SoundTouch> m_stretch;
};
