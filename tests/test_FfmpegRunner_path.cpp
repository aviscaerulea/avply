// FfmpegRunner::ffprobePath ユニットテスト
// プロセス起動を伴わない文字列処理（拡張子置換と相対パス絶対化）のみ検証する

#include <QtTest/QtTest>
#include <QDir>
#include <QFileInfo>

#include "FfmpegRunner.h"

class TestFfmpegRunnerPath : public QObject
{
    Q_OBJECT

private slots:
    void replacesFfmpegWithFfprobe();
    void preservesSpaceInPath();
    void absolutizesRelativePath();
};

void TestFfmpegRunnerPath::replacesFfmpegWithFfprobe()
{
    const QString in  = "C:/x/bin/ffmpeg.exe";
    const QString out = Ffmpeg::ffprobePath(in);
    QCOMPARE(QFileInfo(out).fileName(), QString("ffprobe.exe"));
    QCOMPARE(QFileInfo(out).absolutePath(), QString("C:/x/bin"));
}

void TestFfmpegRunnerPath::preservesSpaceInPath()
{
    const QString in  = "C:/Program Files/x/ffmpeg.exe";
    const QString out = Ffmpeg::ffprobePath(in);
    QCOMPARE(QFileInfo(out).fileName(), QString("ffprobe.exe"));
    QCOMPARE(QFileInfo(out).absolutePath(), QString("C:/Program Files/x"));
}

void TestFfmpegRunnerPath::absolutizesRelativePath()
{
    // 相対パスは CWD ベースで絶対化される
    const QString in  = "ffmpeg.exe";
    const QString out = Ffmpeg::ffprobePath(in);
    QCOMPARE(QFileInfo(out).fileName(), QString("ffprobe.exe"));
    QCOMPARE(QFileInfo(out).absolutePath(),
             QDir::current().absolutePath());
}

QTEST_GUILESS_MAIN(TestFfmpegRunnerPath)
#include "test_FfmpegRunner_path.moc"
