#pragma once
#include <QSlider>

// 開始〜終了の区間をシークバー上に赤系でハイライト表示するスライダー
// 変換時は区間内に進捗オーバーレイ（青）を重ねて描画する
class RangeSlider : public QSlider {
    Q_OBJECT
public:
    explicit RangeSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    // 区間マーカーを設定する。inRatio / outRatio は 0.0〜1.0
    void setRangeMarkers(double inRatio, double outRatio);

    // マーカーをクリアして区間描画を停止する
    void clearRangeMarkers();

    // 進捗オーバーレイを設定する（0〜100）
    // 区間内を左から進捗率に応じて青で塗りつぶす
    void setProgress(int pct);

    // 進捗オーバーレイをクリアする（区間ハイライトのみの表示に戻す）
    void clearProgress();

signals:
    // マウスホイール回転時に emit する。forward = true で前転（早送り方向）
    void wheelScrolled(bool forward);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool   m_hasRange    = false;
    double m_inRatio     = 0.0;
    double m_outRatio    = 0.0;
    bool   m_hasProgress = false;
    int    m_progressPct = 0;
};
