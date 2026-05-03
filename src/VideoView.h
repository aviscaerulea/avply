#pragma once
#include <QWidget>
#include <QString>

class QVideoWidget;
class QMediaPlayer;
class QAudioOutput;
class QDragEnterEvent;
class QDropEvent;

// QMediaPlayer + QVideoWidget + QAudioOutput を束ねた動画プレビュー
// 音声付き再生・シーク・状態通知・D&D 受付を担う
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);
    ~VideoView() override;

    // 動画ファイルを読み込む（自動再生はしない。読み込み完了後に最初のフレームを表示する）
    void setSource(const QString& filePath);

    // ソースをクリアして待機状態に戻す
    void clear();

    // 現在の再生位置をミリ秒で返す
    qint64 position() const;

    // 再生位置（ミリ秒）を変更する
    void setPosition(qint64 ms);

    // 再生速度を変更する（1.0 が等速）
    void setPlaybackRate(qreal rate);

    // 再生状態を切り替える（再生中なら停止、停止中なら再生）
    void togglePlay();

    // 再生中なら一時停止する
    void pause();

    bool isPlaying() const;

    // デフォルトサイズ
    // 起動直後のレイアウト計算で 16:9 800x450 を初期サイズとする
    QSize sizeHint() const override;

    // 縮小限界
    // ユーザリサイズで極端に小さくならないようにする
    QSize minimumSizeHint() const override;

protected:
    // プレビュー領域の左クリックで再生/停止トグル、D&D イベントを捕捉する
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    // 再生位置が変化したとき発火する（ms 単位）
    void positionChanged(qint64 ms);

    // 再生状態が変化したとき発火する（true=再生中）
    void playbackStateChanged(bool playing);

    // プレビュー領域にファイルがドロップされたとき発火する
    void fileDropped(const QString& path);

private:
    QVideoWidget* m_videoWidget;
    QMediaPlayer* m_player;
    QAudioOutput* m_audio;

    // 読み込み完了後に 1 フレームだけ描画するためのフラグ
    bool m_primeFirstFrame = false;
};
