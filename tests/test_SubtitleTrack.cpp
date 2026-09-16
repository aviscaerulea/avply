// SubtitleTrack ユニットテスト
// 再生位置からの検索と SRT の往復変換を検証する

#include <QtTest/QtTest>

#include "SubtitleTrack.h"

class TestSubtitleTrack : public QObject
{
    Q_OBJECT

private slots:
    // textAt
    void textAt_insideCue_returnsText();
    void textAt_gap_returnsEmpty();
    void textAt_endExclusive_returnsEmpty();
    void textAt_overlap_prefersLatestStart();
    void textAt_unorderedAppend_sorted();

    // SRT
    void srt_roundTrip_preservesCues();
    void fromSrt_crlfAndMultilineBody_joined();
    void fromSrt_blockWithoutTime_skipped();
};

void TestSubtitleTrack::textAt_insideCue_returnsText()
{
    SubtitleTrack t;
    t.append({1000, 3000, "a"});
    QCOMPARE(t.textAt(1000), QString("a"));
    QCOMPARE(t.textAt(2999), QString("a"));
}

void TestSubtitleTrack::textAt_gap_returnsEmpty()
{
    SubtitleTrack t;
    t.append({1000, 3000, "a"});
    t.append({5000, 6000, "b"});
    QVERIFY(t.textAt(0).isEmpty());
    QVERIFY(t.textAt(4000).isEmpty());
    QVERIFY(t.textAt(7000).isEmpty());
}

void TestSubtitleTrack::textAt_endExclusive_returnsEmpty()
{
    SubtitleTrack t;
    t.append({1000, 3000, "a"});
    QVERIFY(t.textAt(3000).isEmpty());
}

void TestSubtitleTrack::textAt_overlap_prefersLatestStart()
{
    SubtitleTrack t;
    t.append({1000, 3000, "a"});
    t.append({2500, 4000, "b"});
    QCOMPARE(t.textAt(2000), QString("a"));
    QCOMPARE(t.textAt(2600), QString("b"));
    QCOMPARE(t.textAt(3500), QString("b"));
}

void TestSubtitleTrack::textAt_unorderedAppend_sorted()
{
    SubtitleTrack t;
    t.append({5000, 6000, "b"});
    t.append({1000, 2000, "a"});
    QCOMPARE(t.cues().front().text, QString("a"));
    QCOMPARE(t.textAt(1500), QString("a"));
    QCOMPARE(t.textAt(5500), QString("b"));
}

void TestSubtitleTrack::srt_roundTrip_preservesCues()
{
    SubtitleTrack t;
    t.append({0, 1500, QString::fromUtf8("最初の行")});
    t.append({3600000 + 5, 3600000 + 999, "second"});

    const QString srt = t.toSrt();
    QVERIFY(srt.startsWith("1\n00:00:00,000 --> 00:00:01,500\n"));
    QVERIFY(srt.contains("\n2\n01:00:00,005 --> 01:00:00,999\nsecond\n\n"));

    const SubtitleTrack back = SubtitleTrack::fromSrt(srt);
    QCOMPARE(back.size(), 2);
    QCOMPARE(back.cues()[0].startMs, qint64(0));
    QCOMPARE(back.cues()[0].endMs,   qint64(1500));
    QCOMPARE(back.cues()[0].text,    QString::fromUtf8("最初の行"));
    QCOMPARE(back.cues()[1].startMs, qint64(3600005));
    QCOMPARE(back.cues()[1].text,    QString("second"));
}

void TestSubtitleTrack::fromSrt_crlfAndMultilineBody_joined()
{
    const QString srt = "1\r\n00:00:01,000 --> 00:00:02,000\r\nline one\r\nline two\r\n\r\n";
    const SubtitleTrack t = SubtitleTrack::fromSrt(srt);
    QCOMPARE(t.size(), 1);
    QCOMPARE(t.cues()[0].text, QString("line one line two"));
}

void TestSubtitleTrack::fromSrt_blockWithoutTime_skipped()
{
    const QString srt = "garbage\n\n1\n00:00:01,000 --> 00:00:02,000\nok\n\n2\nno time here\n\n";
    const SubtitleTrack t = SubtitleTrack::fromSrt(srt);
    QCOMPARE(t.size(), 1);
    QCOMPARE(t.cues()[0].text, QString("ok"));
}

QTEST_GUILESS_MAIN(TestSubtitleTrack)
#include "test_SubtitleTrack.moc"
