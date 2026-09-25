#pragma once
#include <QWidget>
#include <QString>
#include <QTimer>

class QQuickView;
class QMediaPlayer;
class QAudioBufferOutput;
class QMediaDevices;
class QThread;
class AudioWorker;
class QWheelEvent;

// QMediaPlayer + QQuickView (VideoOutput) + AudioWorker を束ねた動画プレビュー
// 音声付き再生・シーク・状態通知・D&D 受付を担う。
// QQuickView の threaded render loop により Win32 modal size/move loop 中も描画が継続する
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);
    ~VideoView() override;

    // メディアファイルを読み込む（ロード完了後に自動再生を開始する）
    // 同一ファイルが再投入された場合も先頭から再生する
    void setSource(const QString& filePath);

    // ソースをクリアして待機状態に戻す
    // keepVisible=true でプレビューコンテナを隠さない（同名上書き直後の再ロードなど、
    // 直後に同等の映像を開き直す経路でのチラつき軽減用）
    void clear(bool keepVisible = false);

    // 現在の再生位置をミリ秒で返す
    qint64 position() const;

    // 再生位置（ミリ秒）を変更する
    // AudioWorker::reset へ目標位置を渡し、シーク前のバッファを破棄させる
    void setPosition(qint64 ms);

    // 再生速度を変更する（1.0 が等速）
    void setPlaybackRate(qreal rate);

    // 再生音量を設定する（0.0〜1.0、範囲外はクランプ）
    void setVolume(double volume);

    // 再生状態を切り替える（再生中なら停止、停止中なら再生）
    void togglePlay();

    // 再生中なら一時停止する
    // AudioWorker::pauseOutput を投げ、sink に残る音声を無音ランプで終わらせる（クリック対策）
    void pause();

    // 停止中なら再生を開始する（末尾到達状態からは先頭再生）
    // AudioWorker::resumeOutput で一時停止ゲートを閉じてから再生する
    void play();

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

    // 音声強調（WebRTC APM）の ON/OFF を設定する
    // 変更は AudioWorker に QueuedConnection で転送され、audio thread 上で ApplyConfig される
    void setSpeechEnhanceEnabled(bool enabled);

    // 映像上に重ねる字幕テキストを設定する（空文字で非表示）
    // QML ルートの subtitleText プロパティへ書くだけで、表示位置・折り返しは QML 側が担う
    void setSubtitleText(const QString& text);

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
    // shift = true は Shift 修飾子押下中（音量調整用）、ctrl = true は Ctrl 修飾子押下中（再生速度調整用）
    void wheelScrolled(bool forward, bool shift, bool ctrl);

    // メディアのロードに失敗したとき emit する（InvalidMedia 遷移時）
    // error は QMediaPlayer::errorString() の内容
    void loadFailed(const QString& error);

    // QQuickView が初回フレームを present したとき 1 回だけ emit する
    // MainWindow が起動時の透明化を解除する契機に使う。
    // 音声拡張子で起動して VideoView が非表示のままの場合は emit されない
    void firstFrameRendered();

    // 映像フレームが video sink へ届くたびに emit する（GUI thread へ queued 配送）
    // MainWindow がシークバードラッグ中のシーク発行を、直前シークのフレーム到達まで待つために使う
    void videoFrameArrived();

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
    void onQmlWheelScrolled(bool forward, bool shift, bool ctrl);

    void onQmlFileDropped(const QString& url);

private:
    // AudioWorker::pauseOutput を audio thread へ投げる
    // ユーザ操作の一時停止（pause）と末尾到達の自動停止の両方から呼ぶ
    void requestPauseOutput();

    QQuickView*         m_quickView;
    QWidget*            m_videoContainer = nullptr;
    QMediaPlayer*       m_player;
    QAudioBufferOutput* m_audioBuf    = nullptr;
    QThread*            m_audioThread = nullptr;
    AudioWorker*        m_audioWorker = nullptr;

    // ロード完了検知フラグ
    // 完了時に自動再生を開始し、映像なしならプレビューコンテナを隠す。
    // ロード中は旧ソースの遅延 positionChanged を破棄するゲートも兼ねる
    bool m_primeFirstFrame = false;

    // マウスクリックでの再生トグル許可フラグ
    bool m_interactive = true;

    // 末尾到達時の自動 pause 再入防止フラグ
    // pause() は非同期完了のため、直後の positionChanged で isPlaying() がまだ true を
    // 返す場合がある。フラグで一度だけ pause を発火させ、ソース切替時にリセットする
    bool m_pausingAtEnd = false;

    // デフォルト出力デバイス切替の検知用
    // QMediaDevices はシグナル購読のためインスタンスが要る（静的関数だけでは通知を受けられない）
    QMediaDevices* m_mediaDevices = nullptr;

    // audioOutputsChanged の連続発火を集約する debounce タイマ
    // BT 接続シーケンス中は短時間に複数回通知が来るため、audio thread への通知を
    // 最後の 1 回へ集約する。デストラクタ冒頭で停止し、破棄中の発火を握り潰す
    QTimer m_deviceChangeDebounce;
};
