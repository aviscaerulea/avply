#pragma once
#include <QSlider>
#include <QPixmap>

// MPC-HC 風の 2 段構成シークバー
// 上段（kTrackH）：暗背景 + 波形 PNG + 再生済みグレーオーバーレイ + 現在位置の青縦棒
// 下段（kRangeBarH）：開始〜終了の区間赤バー + 変換進捗の青オーバーレイ（中止時は青のみ消去）
class RangeSlider : public QSlider {
    Q_OBJECT
public:
    // 上段（MPC-HC 風トラック）の高さ
    static constexpr int kTrackH = 28;

    // 下段（区間・進捗帯）の高さ
    static constexpr int kRangeBarH = 8;

    // ウィジェット全体の固定高
    static constexpr int kTotalH = kTrackH + kRangeBarH;

    explicit RangeSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    // 固定高（kTotalH）を強制するためのサイズヒント
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // 区間マーカーを設定する。inRatio / outRatio は 0.0〜1.0
    void setRangeMarkers(double inRatio, double outRatio);

    // マーカーをクリアして区間描画を停止する
    void clearRangeMarkers();

    // 進捗オーバーレイを設定する（0〜100）
    // 区間内を左から進捗率に応じて青で塗りつぶす
    void setProgress(int pct);

    // 進捗オーバーレイをクリアする（区間ハイライトのみの表示に戻す）
    void clearProgress();

    // 音声波形 Pixmap を設定する
    // 上段トラック矩形（kTrackH 高）にスケール描画される
    void setWaveform(const QPixmap& pix);

    // 音声無し時の中央基線描画モードを有効化する
    void setBaseline(bool enabled);

    // 波形・基線表示をクリアする（ファイル切替時のリセット用）
    void clearWaveform();

signals:
    // マウスホイール回転時に emit する。forward = true で前転（早送り方向）
    void wheelScrolled(bool forward);

    // ホバー位置（widget 内 X 座標、クランプ済み）と対応スライダー値を通知する
    // ボタン非押下時もドラッグ中も連続的に発火する
    void hoverMoved(int x, int sliderValue);

    // マウスがバー外に出たことを通知する
    void hoverLeft();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    // MPC-HC 風のクリック挙動（バー上のクリック位置へ即時シーク）
    // QSlider 標準の groove rect 限定ヒットテストではなく、widget 全幅で受け付ける
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool   m_hasRange    = false;
    double m_inRatio     = 0.0;
    double m_outRatio    = 0.0;
    bool   m_hasProgress = false;
    int    m_progressPct = 0;

    // 音声波形 Pixmap（空のときは描画しない）
    QPixmap m_waveform;

    // 音声無し時の中央基線描画フラグ
    bool m_drawBaseline = false;
};
