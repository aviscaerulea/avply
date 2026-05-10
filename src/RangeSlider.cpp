#include "RangeSlider.h"
#include <QPainter>
#include <QStyle>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>

namespace {

// 描画色
// 上段：トラック背景／音声なし時の中央基線／再生済みオーバーレイ／現在位置インジケータ
// 下段：区間ハイライト（赤）／進捗オーバーレイ（青）
const QColor kTrackBgColor       (0x1A, 0x1A, 0x1A);
const QColor kBaselineColor      (180, 180, 180, 120);
const QColor kPlayedOverlayColor (140, 140, 140, 100);
const QColor kIndicatorColor     (63,  169, 245);
const QColor kRangeColor         (220, 60,  60,  220);
const QColor kProgressColor      (60,  130, 220, 220);

} // namespace

RangeSlider::RangeSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    // ボタン非押下時の mouseMoveEvent を受信するためにホバートラッキングを有効化する
    // ホバープレビューがボタン押下を待たずにマウス追従できるようにするための前提
    setMouseTracking(true);
}

QSize RangeSlider::sizeHint() const
{
    // 横幅は親レイアウトのストレッチに任せ、固定高のみ強制する
    return QSize(1, kTotalH);
}

QSize RangeSlider::minimumSizeHint() const
{
    return QSize(1, kTotalH);
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

void RangeSlider::setProgress(int pct)
{
    m_progressPct = std::clamp(pct, 0, 100);
    m_hasProgress = true;
    update();
}

void RangeSlider::clearProgress()
{
    m_hasProgress = false;
    m_progressPct = 0;
    update();
}

void RangeSlider::setWaveform(const QPixmap& pix)
{
    m_waveform = pix;
    m_drawBaseline = false;
    update();
}

void RangeSlider::setBaseline(bool enabled)
{
    m_drawBaseline = enabled;
    if (enabled) m_waveform = QPixmap();
    update();
}

void RangeSlider::clearWaveform()
{
    m_waveform = QPixmap();
    m_drawBaseline = false;
    update();
}

void RangeSlider::wheelEvent(QWheelEvent* event)
{
    // QSlider デフォルトのホイール動作（値変更）を抑制してシーク信号に変換する
    const int delta = event->angleDelta().y();
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if (delta != 0) emit wheelScrolled(delta > 0, shift);
    event->accept();
}

void RangeSlider::mousePressEvent(QMouseEvent* event)
{
    // MPC-HC 風：左クリック位置へ即時ジャンプ
    // QStyle::sliderValueFromPosition で widget 全幅を span として値を逆算する。
    // 初期化フレームで width()<=0 の場合に整数除算を踏まないようガードする
    if (event->button() == Qt::LeftButton && width() > 0) {
        const int x = static_cast<int>(event->position().x());
        const int v = QStyle::sliderValueFromPosition(
            minimum(), maximum(), x, width());
        setSliderDown(true);
        setValue(v);
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void RangeSlider::mouseMoveEvent(QMouseEvent* event)
{
    // ドラッグ中は連続的に位置追従させる
    if ((event->buttons() & Qt::LeftButton) && width() > 0) {
        const int x = static_cast<int>(event->position().x());
        const int v = QStyle::sliderValueFromPosition(
            minimum(), maximum(), x, width());
        setValue(v);
        // ドラッグ中もホバー通知を出す（MPC-HC 互換でプレビューが追従する）
        emit hoverMoved(std::clamp(x, 0, width() - 1), v);
        event->accept();
        return;
    }
    // ボタン非押下のホバー通知。クランプ済み X からスライダー値を逆算する
    if (width() > 0) {
        const int x  = static_cast<int>(event->position().x());
        const int xc = std::clamp(x, 0, width() - 1);
        const int v  = QStyle::sliderValueFromPosition(
            minimum(), maximum(), xc, width());
        emit hoverMoved(xc, v);
    }
    QSlider::mouseMoveEvent(event);
}

void RangeSlider::leaveEvent(QEvent* event)
{
    emit hoverLeft();
    QSlider::leaveEvent(event);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent* event)
{
    // ドラッグ終端で sliderReleased を発火させる（既存 sliderDown 状態を解除）
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setSliderDown(false);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void RangeSlider::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);

    const QRect trackRect(0, 0, width(), kTrackH);
    const QRect rangeRect(0, kTrackH, width(), kRangeBarH);

    // --- 上段：MPC-HC 風トラック ---

    // トラック背景（暗灰色）。アプリ背景より暗くして輪郭を出す
    painter.fillRect(trackRect, kTrackBgColor);

    // 波形 PNG をトラック全体にスケール描画。波形なし時は中央基線を 1px 描画
    if (!m_waveform.isNull()) {
        painter.drawPixmap(trackRect, m_waveform);
    }
    else if (m_drawBaseline) {
        const int y = trackRect.center().y();
        painter.fillRect(QRect(trackRect.left(), y, trackRect.width(), 1),
                         kBaselineColor);
    }

    // 現在位置の x 座標を slider 値から逆算する
    const int handleX = QStyle::sliderPositionFromValue(
        minimum(), maximum(), value(), width());

    // 再生済み部分（左端〜現在位置）に半透明グレーを被せて視認性を上げる
    if (handleX > 0) {
        painter.fillRect(QRect(0, 0, handleX, kTrackH), kPlayedOverlayColor);
    }

    // 現在位置インジケータ（幅 2px の青縦棒、トラック全高）
    // 端で半切れにならないよう [1, width()-1] にクランプして 2px 全体が widget 内に収まるようにする
    if (width() >= 2) {
        const int indicatorX = std::clamp(handleX, 1, width() - 1);
        painter.fillRect(QRect(indicatorX - 1, 0, 2, kTrackH), kIndicatorColor);
    }

    // --- 下段：区間・進捗帯 ---

    if (m_hasRange && m_outRatio > m_inRatio) {
        const int x1 = static_cast<int>(width() * m_inRatio);
        const int x2 = static_cast<int>(width() * m_outRatio);
        painter.fillRect(QRect(x1, rangeRect.y(), x2 - x1, kRangeBarH), kRangeColor);

        // 進捗オーバーレイ（青）を区間内に重ねる。100% で区間全体が青に見える
        if (m_hasProgress) {
            const int progressW = static_cast<int>((x2 - x1) * m_progressPct / 100.0);
            if (progressW > 0) {
                painter.fillRect(QRect(x1, rangeRect.y(), progressW, kRangeBarH),
                                 kProgressColor);
            }
        }
    }
}
