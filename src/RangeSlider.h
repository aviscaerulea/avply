#pragma once
#include <QSlider>

// 開始〜終了の区間をシークバー上に黄色でハイライト表示するスライダー
class RangeSlider : public QSlider {
    Q_OBJECT
public:
    explicit RangeSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    // 区間マーカーを設定する。inRatio / outRatio は 0.0〜1.0
    void setRangeMarkers(double inRatio, double outRatio);

    // マーカーをクリアして区間描画を停止する
    void clearRangeMarkers();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool   m_hasRange  = false;
    double m_inRatio   = 0.0;
    double m_outRatio  = 0.0;
};
