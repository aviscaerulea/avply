// OutputNamer ユニットテスト
// QTemporaryDir 上で実ファイル衝突を作り、_mod シーケンス生成と UUID フォールバックを検証する

#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "OutputNamer.h"

namespace {

// 指定パスに空ファイルを作成する
// QFile のスコープ抜け時にハンドルを閉じてからリネーム / 削除が可能になることを保証する
bool touch(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();
    return true;
}

} // namespace

class TestOutputNamer : public QObject
{
    Q_OBJECT

private slots:
    // isModName の基本判定
    void isModName_plainBaseName_returnsFalse();
    void isModName_modSuffix_returnsTrue();
    void isModName_modWithNumber_returnsTrue();
    void isModName_modInMiddle_returnsFalse();

    // generate の基本動作
    void generate_noCollision_appendsMod();
    void generate_changesExtension();
    void generate_modInput_returnsSameName();
    void generate_modNumberedInput_returnsSameName();

    // 衝突時の連番生成
    void generate_oneCollision_returnsMod2();
    void generate_threeCollisions_returnsMod4();

    // 日本語 / スペースを含むパス
    void generate_unicodeAndSpacePath();

    // UUID フォールバック
    void generate_uuidFallback_after100Collisions();

    // 異なる拡張子は衝突扱いしない
    void generate_differentExtensionDoesNotCollide();
};

void TestOutputNamer::isModName_plainBaseName_returnsFalse()
{
    QVERIFY(!OutputNamer::isModName("C:/tmp/foo.mp4"));
}

void TestOutputNamer::isModName_modSuffix_returnsTrue()
{
    QVERIFY(OutputNamer::isModName("C:/tmp/foo_mod.mp4"));
}

void TestOutputNamer::isModName_modWithNumber_returnsTrue()
{
    QVERIFY(OutputNamer::isModName("C:/tmp/foo_mod7.mp4"));
    QVERIFY(OutputNamer::isModName("C:/tmp/foo_mod100.mp4"));
}

void TestOutputNamer::isModName_modInMiddle_returnsFalse()
{
    // _mod が末尾でなければ判定対象外
    QVERIFY(!OutputNamer::isModName("C:/tmp/foo_mod_bar.mp4"));
}

void TestOutputNamer::generate_noCollision_appendsMod()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mp4";

    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(QFileInfo(out).fileName(), QString("foo_mod.mp4"));
    QCOMPARE(QFileInfo(out).absolutePath(), QFileInfo(input).absolutePath());
}

void TestOutputNamer::generate_changesExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mkv";

    const QString out = OutputNamer::generate(input, "opus");
    QCOMPARE(QFileInfo(out).fileName(), QString("foo_mod.opus"));
}

void TestOutputNamer::generate_modInput_returnsSameName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo_mod.mp4";
    QVERIFY(touch(input));

    // _mod 付き入力は衝突を無視して同名上書きを返す
    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(out, input);
}

void TestOutputNamer::generate_modNumberedInput_returnsSameName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo_mod3.mp4";
    QVERIFY(touch(input));

    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(out, input);
}

void TestOutputNamer::generate_oneCollision_returnsMod2()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mp4";
    QVERIFY(touch(dir.path() + "/foo_mod.mp4"));

    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(QFileInfo(out).fileName(), QString("foo_mod2.mp4"));
}

void TestOutputNamer::generate_threeCollisions_returnsMod4()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mp4";
    QVERIFY(touch(dir.path() + "/foo_mod.mp4"));
    QVERIFY(touch(dir.path() + "/foo_mod2.mp4"));
    QVERIFY(touch(dir.path() + "/foo_mod3.mp4"));

    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(QFileInfo(out).fileName(), QString("foo_mod4.mp4"));
}

void TestOutputNamer::generate_unicodeAndSpacePath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/テスト 動画.mp4";

    const QString out = OutputNamer::generate(input, "mp4");
    QCOMPARE(QFileInfo(out).fileName(), QString("テスト 動画_mod.mp4"));
}

void TestOutputNamer::generate_uuidFallback_after100Collisions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mp4";

    // _mod および _mod2 〜 _mod100 を全て衝突させて UUID フォールバックを発火させる
    QVERIFY(touch(dir.path() + "/foo_mod.mp4"));
    for (int n = 2; n <= 100; ++n) {
        QVERIFY(touch(dir.path() + QString("/foo_mod%1.mp4").arg(n)));
    }

    const QString out = OutputNamer::generate(input, "mp4");
    const QString name = QFileInfo(out).fileName();
    // foo_mod_<UUID>.mp4 形式（UUID は 36 文字の小文字 + ハイフン）
    const QRegularExpression re("^foo_mod_[0-9a-f-]{36}\\.mp4$");
    QVERIFY2(re.match(name).hasMatch(),
             qPrintable("UUID フォールバック形式不一致: " + name));
}

void TestOutputNamer::generate_differentExtensionDoesNotCollide()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.path() + "/foo.mkv";
    // 異なる拡張子の同名は衝突扱いしない（変換は拡張子が変わる）
    QVERIFY(touch(dir.path() + "/foo_mod.mp4"));

    const QString out = OutputNamer::generate(input, "opus");
    QCOMPARE(QFileInfo(out).fileName(), QString("foo_mod.opus"));
}

QTEST_GUILESS_MAIN(TestOutputNamer)
#include "test_OutputNamer.moc"
