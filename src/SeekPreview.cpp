#include "SeekPreview.h"
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>

SeekPreview::SeekPreview(QWidget* parent)
    : QWidget(parent,
              Qt::ToolTip
              | Qt::FramelessWindowHint
              | Qt::WindowDoesNotAcceptFocus)
{
    // フォーカスを奪わず、マウスイベントもすべて素通しさせる
    // → 親ウィンドウのシーク操作をプレビュー表示が妨げないようにする
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_timeLabel = new QLabel(this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timeLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addWidget(m_thumbLabel);
    layout->addWidget(m_timeLabel);

    // 黒半透明背景 + 白枠 1px。MPC-HC のホバープレビュー外観に近い
    setStyleSheet(
        "SeekPreview {"
        "  background-color: rgba(0, 0, 0, 220);"
        "  border: 1px solid rgba(255, 255, 255, 180);"
        "}");

    // 既定では非表示
    hide();
}

void SeekPreview::setContent(const QPixmap& thumb, const QString& timeText)
{
    if (thumb.isNull()) {
        m_thumbLabel->clear();
        m_thumbLabel->hide();
    }
    else {
        m_thumbLabel->setPixmap(thumb);
        m_thumbLabel->setFixedSize(thumb.size());
        m_thumbLabel->show();
    }
    m_timeLabel->setText(timeText);
    adjustSize();
}

void SeekPreview::setTimeOnly(const QString& timeText)
{
    m_timeLabel->setText(timeText);
    // サムネイル状態は維持。adjustSize() で時刻文字列幅変動による再フィットだけ行う
    adjustSize();
}

void SeekPreview::showAt(const QPoint& cursorGlobal,
                         const QRect& sliderGlobal,
                         const QRect& screenAvail)
{
    adjustSize();
    const QSize sz = size();
    constexpr int kMargin = 6;

    // 水平：カーソル X を中心に。画面端でクランプして全体が画面内に収まるようにする
    int x = cursorGlobal.x() - sz.width() / 2;
    if (!screenAvail.isEmpty()) {
        const int xMin = screenAvail.left() + kMargin;
        const int xMax = screenAvail.right() - sz.width() - kMargin;
        if (xMax >= xMin) x = std::clamp(x, xMin, xMax);
    }

    // 垂直：シークバー直上に吸着。上端からはみ出すならシークバー直下に回す
    int y = sliderGlobal.top() - sz.height() - kMargin;
    if (!screenAvail.isEmpty() && y < screenAvail.top() + kMargin) {
        y = sliderGlobal.bottom() + kMargin;
    }

    move(x, y);
    if (!isVisible()) show();
}
