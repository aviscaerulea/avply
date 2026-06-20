// SeekPreview ユニットテスト
// showAt のジオメトリ算出（中央配置・画面端クランプ・上端フリップ）を検証する

#include <QtTest/QtTest>
#include <QPixmap>
#include <QPoint>
#include <QRect>

#include "SeekPreview.h"

namespace {

// テスト固定の入力サムネ・時刻
SeekPreview* makePreview()
{
    auto* p = new SeekPreview();
    p->setContent(QPixmap(80, 45), "00:00:30");
    return p;
}

} // namespace

class TestSeekPreview : public QObject
{
    Q_OBJECT

private slots:
    void showAt_centersOnCursorAndAboveSlider();
    void showAt_clampsToScreenLeft();
    void showAt_clampsToScreenRight();
    void showAt_flipsBelowWhenSliderAtTop();
};

void TestSeekPreview::showAt_centersOnCursorAndAboveSlider()
{
    SeekPreview* preview = makePreview();

    const QPoint cursor(500, 500);
    const QRect  slider(100, 480, 600, 20);
    const QRect  screen(0, 0, 1920, 1080);
    preview->showAt(cursor, slider, screen);

    const QSize sz = preview->size();
    QCOMPARE(preview->x(), cursor.x() - sz.width() / 2);
    QCOMPARE(preview->y(), slider.top() - sz.height() - SeekPreview::kMargin);
    QVERIFY(preview->isVisible());

    delete preview;
}

void TestSeekPreview::showAt_clampsToScreenLeft()
{
    SeekPreview* preview = makePreview();

    // 画面左端付近のカーソル。中央配置だと x が負になるためクランプされる
    const QPoint cursor(10, 500);
    const QRect  slider(0, 480, 1920, 20);
    const QRect  screen(0, 0, 1920, 1080);
    preview->showAt(cursor, slider, screen);

    QCOMPARE(preview->x(), screen.left() + SeekPreview::kMargin);

    delete preview;
}

void TestSeekPreview::showAt_clampsToScreenRight()
{
    SeekPreview* preview = makePreview();

    // 画面右端付近のカーソル。中央配置だと右側がはみ出るためクランプされる
    const QPoint cursor(1910, 500);
    const QRect  slider(0, 480, 1920, 20);
    const QRect  screen(0, 0, 1920, 1080);
    preview->showAt(cursor, slider, screen);

    const QSize sz = preview->size();
    QCOMPARE(preview->x(), screen.right() - sz.width() - SeekPreview::kMargin);

    delete preview;
}

void TestSeekPreview::showAt_flipsBelowWhenSliderAtTop()
{
    SeekPreview* preview = makePreview();

    // スライダーが画面最上端にあり、上に表示するマージンを取れないため下へ回る
    const QPoint cursor(500, 5);
    const QRect  slider(100, 0, 600, 20);
    const QRect  screen(0, 0, 1920, 1080);
    preview->showAt(cursor, slider, screen);

    QCOMPARE(preview->y(), slider.bottom() + SeekPreview::kMargin);

    delete preview;
}

QTEST_MAIN(TestSeekPreview)
#include "test_SeekPreview.moc"
