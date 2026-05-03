#include "RangeSlider.h"
#include <QPainter>
#include <QStyleOptionSlider>
#include <QStylePainter>
#include <algorithm>

RangeSlider::RangeSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
}

void RangeSlider::setRangeMarkers(double inRatio, double outRatio)
{
    m_inRatio  = std::clamp(inRatio, 0.0, 1.0);
    m_outRatio = std::clamp(outRatio, 0.0, 1.0);
    if (m_outRatio < m_inRatio) std::swap(m_inRatio, m_outRatio);
    m_hasRange = true;
    update();
}

void RangeSlider::clearRangeMarkers()
{
    m_hasRange = false;
    update();
}

void RangeSlider::paintEvent(QPaintEvent* /*event*/)
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QStylePainter painter(this);

    // groove だけ先に描画する
    QStyleOptionSlider grooveOpt = opt;
    grooveOpt.subControls = QStyle::SC_SliderGroove;
    painter.drawComplexControl(QStyle::CC_Slider, grooveOpt);

    // 開始〜終了の区間を赤系でハイライト
    if (m_hasRange && m_outRatio > m_inRatio) {
        const QRect groove = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const int x1 = groove.left() + static_cast<int>(groove.width() * m_inRatio);
        const int x2 = groove.left() + static_cast<int>(groove.width() * m_outRatio);
        const int y  = groove.center().y() - 3;
        const QRect rangeRect(x1, y, x2 - x1, 7);
        painter.fillRect(rangeRect, QColor(220, 60, 60, 220));
    }

    // ハンドルを最後に描いて区間ハイライトより前面に表示する
    QStyleOptionSlider handleOpt = opt;
    handleOpt.subControls = QStyle::SC_SliderHandle;
    painter.drawComplexControl(QStyle::CC_Slider, handleOpt);
}
