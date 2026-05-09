#pragma once
#include <QWidget>
#include <QString>

class QQuickView;
class QMediaPlayer;
class QAudioOutput;
class QWheelEvent;

// QMediaPlayer + QQuickView (VideoOutput) + QAudioOutput を束ねた動画プレビュー
// 音声付き再生・シーク・状態通知・D&D 受付を担う。
// QQuickView の threaded render loop により Win32 modal size/move loop 中も描画が継続する
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);
    ~VideoView() override;

    // メディアファイルを読み込む（自動再生はしない。読み込み完了後に最初のフレームを表示する）
    void setSource(const QString& filePath);

    // ソースをクリアして待機状態に戻す
    void clear();

    // 現在の再生位置をミリ秒で返す
    qint64 position() const;

    // 再生位置（ミリ秒）を変更する
    void setPosition(qint64 ms);

    // 再生速度を変更する（1.0 が等速）
    void setPlaybackRate(qreal rate);

    // 再生音量を設定する（0.0〜1.0、範囲外はクランプ）
    void setVolume(double volume);

    // 再生状態を切り替える（再生中なら停止、停止中なら再生）
    void togglePlay();

    // 再生中なら一時停止する
    void pause();

    bool isPlaying() const;

    // プレビュー領域へのマウスクリックでの再生トグルを許可するか設定する
    // 変換中などに UI 操作を抑止する用途。デフォルトは true（許可）
    void setInteractive(bool enabled);

    // デフォルトサイズ
    // 起動直後のレイアウト計算で 16:9 800x450 を初期サイズとする
    QSize sizeHint() const override;

    // 縮小限界
    // ユーザリサイズで極端に小さくならないようにする
    QSize minimumSizeHint() const override;

protected:
    void wheelEvent(QWheelEvent* event) override;

signals:
    // 再生位置が変化したとき発火する（ms 単位）
    void positionChanged(qint64 ms);

    // 再生状態が変化したとき発火する（true=再生中）
    void playbackStateChanged(bool playing);

    // プレビュー領域にファイルがドロップされたとき発火する
    void fileDropped(const QString& path);

    // マウスホイール回転時に emit する。forward = true で前転（早送り方向）
    void wheelScrolled(bool forward);

    // 右クリックでコンテキストメニュー要求が発生したとき emit する
    // QQuickView はネイティブ子ウィンドウのため Win32 が右クリックを親 QWidget へ
    // 伝搬しない。プレビュー上のメニュー表示を実現するため QML 側で受けて転送する
    void contextMenuRequested(const QPoint& globalPos);

private slots:
    // QML VideoOutput.qml の clicked シグナルを受け取り再生トグルに変換する
    void onQmlClicked();

    // QML の contextMenuRequested を受け、ローカル座標を画面座標に変換して再 emit する
    void onQmlContextMenuRequested(qreal x, qreal y);

    // QML の wheelScrolled シグナルをブリッジする
    void onQmlWheelScrolled(bool forward);

    void onQmlFileDropped(const QString& url);

private:
    QQuickView*   m_quickView;
    QWidget*      m_videoContainer = nullptr;
    QMediaPlayer* m_player;
    QAudioOutput* m_audioOutput;

    // 読み込み完了後に 1 フレームだけ描画するためのフラグ
    bool m_primeFirstFrame = false;

    // マウスクリックでの再生トグル許可フラグ
    bool m_interactive = true;

    // 末尾到達時の自動 pause 再入防止フラグ
    // pause() は非同期完了のため、直後の positionChanged で isPlaying() がまだ true を
    // 返す場合がある。フラグで一度だけ pause を発火させ、ソース切替時にリセットする
    bool m_pausingAtEnd = false;
};
