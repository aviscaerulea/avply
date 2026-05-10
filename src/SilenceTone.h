#pragma once
#include <QObject>

class QAudioSink;
class QIODevice;

// Bluetooth コーデック・ヘッドセットのアイドル復帰時プチノイズ抑制用のサイレンストーン出力
// 並列の QAudioSink から不可聴レベルの低振幅トーンを連続再生し、デフォルト音声デバイスを
// 常時アクティブに保つ。これにより BT 機器が無音区間で省電力／コーデックアイドル状態に入り、
// 次の音声開始時に「プチ」とノイズが乗る現象を防ぐ。
// QMediaPlayer の音声出力とは独立した OS ストリームとして動作するため、相互干渉はしない
class SilenceTone : public QObject {
    Q_OBJECT
public:
    explicit SilenceTone(QObject* parent = nullptr);
    ~SilenceTone() override;

    // トーン出力を開始する
    // 既に開始済みの場合は何もしない
    void start();

    // トーン出力を停止する
    void stop();

private:
    QAudioSink* m_sink   = nullptr;
    QIODevice*  m_device = nullptr;
};
