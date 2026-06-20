// Settings ユニットテスト
// QSettings の HKCU 書き込みは実体（HKCU\Software\avply\avply）に対して行う
// 開発者環境を壊さないため、テスト開始時に該当値を退避＋初期化、終了時に復元する
//
// 補足：当初 RegOverridePredefKey による HKCU リダイレクトを試したが、
// QSettings 側の内部キャッシュとリダイレクトの相性で setter の書き込みが
// 反映されない事象が発生したため、確実性を優先して退避＋復元方式に切り替えた

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#define NOMINMAX
#include <windows.h>

#include "Settings.h"

namespace {

// avply の QSettings 配置先（NativeFormat / UserScope / org=avply / app=avply に対応）
constexpr const wchar_t* kAvplyRegPath = L"Software\\avply\\avply";

// 1 値の退避情報
struct ValueBackup
{
    bool       present = false;
    DWORD      type    = 0;
    QByteArray data;
};

// Settings が読み書きする全キー
// 退避と初期化（削除）の対象
const QStringList& trackedKeys()
{
    static const QStringList keys = {
        QStringLiteral("topmostWhilePlaying"),
        QStringLiteral("singleInstance"),
        QStringLiteral("aboveNormalPriority"),
        QStringLiteral("speechEnhanceLevel"),
    };
    return keys;
}

QMap<QString, ValueBackup> g_backups;

// 単一キーの値を退避する
ValueBackup queryValue(HKEY hk, const QString& name)
{
    ValueBackup b;
    DWORD type = 0;
    DWORD size = 0;
    LONG rc = RegQueryValueExW(hk, reinterpret_cast<LPCWSTR>(name.utf16()),
                               nullptr, &type, nullptr, &size);
    if (rc != ERROR_SUCCESS) {
        return b;
    }
    b.present = true;
    b.type = type;
    b.data.resize(static_cast<int>(size));
    RegQueryValueExW(hk, reinterpret_cast<LPCWSTR>(name.utf16()),
                     nullptr, &type,
                     reinterpret_cast<LPBYTE>(b.data.data()), &size);
    return b;
}

// テスト開始時：現状値を退避し、未書込状態へ初期化する
// デフォルト値の検証が既存値の影響を受けないようにする
void backupAndClear()
{
    HKEY hk = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kAvplyRegPath, 0, KEY_ALL_ACCESS, &hk);
    if (rc != ERROR_SUCCESS) {
        // 未作成：全キーが「存在しない」状態として退避する
        for (const QString& k : trackedKeys()) {
            g_backups[k] = ValueBackup{};
        }
        return;
    }
    for (const QString& k : trackedKeys()) {
        g_backups[k] = queryValue(hk, k);
        RegDeleteValueW(hk, reinterpret_cast<LPCWSTR>(k.utf16()));
    }
    RegCloseKey(hk);
}

// テスト終了時：退避値を復元し、テストで書いた値を破棄する
// avply 未起動環境ではキー自体が無いまま QSettings がテスト中に作るため、
// 開ける場合のみ処理する（無条件 RegCreateKeyExW は空キー生成の副作用を残す）
void restore()
{
    HKEY hk = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kAvplyRegPath, 0, KEY_ALL_ACCESS, &hk);
    if (rc != ERROR_SUCCESS) {
        // キー不在＝テスト中も書込みなし。復元・削除どちらも対象なしのため終了
        return;
    }
    for (const QString& k : trackedKeys()) {
        const ValueBackup& b = g_backups[k];
        if (b.present) {
            RegSetValueExW(hk, reinterpret_cast<LPCWSTR>(k.utf16()), 0, b.type,
                           reinterpret_cast<const BYTE*>(b.data.constData()),
                           static_cast<DWORD>(b.data.size()));
        }
        else {
            // 退避時は不在 → テスト中に setter で書かれた値を削除する
            RegDeleteValueW(hk, reinterpret_cast<LPCWSTR>(k.utf16()));
        }
    }
    RegCloseKey(hk);
}

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    // 各ケース冒頭で QSettings のキャッシュとレジストリを揃える
    void init();

    // デフォルト値（未書込時の初期状態）
    void topmost_defaultIsFalse();
    void singleInstance_defaultIsFalse();
    void aboveNormalPriority_defaultIsFalse();
    void speechEnhanceLevel_defaultIsOne();

    // setter / getter のラウンドトリップ
    void topmost_setTrue_persists();
    void singleInstance_setTrue_persists();
    void aboveNormalPriority_setTrue_persists();

    // speechEnhanceLevel のクランプ挙動
    void speechEnhanceLevel_clampsBelowToZero();
    void speechEnhanceLevel_clampsAboveToTwo();
};

void TestSettings::init()
{
    // テスト間で状態を持ち越さないよう、毎ケース実体キーを初期化する
    // 直後の getter は QSettings 内部キャッシュを経由するため、
    // 「未書込時の値」検証は init 直後の最初のアクセスで判定される点に注意する
    HKEY hk = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kAvplyRegPath, 0, KEY_ALL_ACCESS, &hk);
    if (rc == ERROR_SUCCESS) {
        for (const QString& k : trackedKeys()) {
            RegDeleteValueW(hk, reinterpret_cast<LPCWSTR>(k.utf16()));
        }
        RegCloseKey(hk);
    }
}

void TestSettings::topmost_defaultIsFalse()
{
    // 未書込時は false を返す（init で RegDeleteValue した直後の素読みを検証する）
    QCOMPARE(Settings::instance().topmostWhilePlaying(), false);
}

void TestSettings::singleInstance_defaultIsFalse()
{
    QCOMPARE(Settings::instance().singleInstance(), false);
}

void TestSettings::aboveNormalPriority_defaultIsFalse()
{
    QCOMPARE(Settings::instance().aboveNormalPriority(), false);
}

void TestSettings::speechEnhanceLevel_defaultIsOne()
{
    // 仕様：speechEnhanceLevel は未書込時に 1（Standard）を返す
    QCOMPARE(Settings::instance().speechEnhanceLevel(), 1);
}

void TestSettings::topmost_setTrue_persists()
{
    Settings::instance().setTopmostWhilePlaying(true);
    QCOMPARE(Settings::instance().topmostWhilePlaying(), true);
    Settings::instance().setTopmostWhilePlaying(false);
    QCOMPARE(Settings::instance().topmostWhilePlaying(), false);
}

void TestSettings::singleInstance_setTrue_persists()
{
    Settings::instance().setSingleInstance(true);
    QCOMPARE(Settings::instance().singleInstance(), true);
    Settings::instance().setSingleInstance(false);
    QCOMPARE(Settings::instance().singleInstance(), false);
}

void TestSettings::aboveNormalPriority_setTrue_persists()
{
    Settings::instance().setAboveNormalPriority(true);
    QCOMPARE(Settings::instance().aboveNormalPriority(), true);
    Settings::instance().setAboveNormalPriority(false);
    QCOMPARE(Settings::instance().aboveNormalPriority(), false);
}

void TestSettings::speechEnhanceLevel_clampsBelowToZero()
{
    Settings::instance().setSpeechEnhanceLevel(-5);
    QCOMPARE(Settings::instance().speechEnhanceLevel(), 0);
}

void TestSettings::speechEnhanceLevel_clampsAboveToTwo()
{
    Settings::instance().setSpeechEnhanceLevel(10);
    QCOMPARE(Settings::instance().speechEnhanceLevel(), 2);
}

// テスト用エントリポイント
// 退避 → QCoreApplication 構築 → QTest 実行 → 復元 の順で実体 HKCU を保護する
int main(int argc, char** argv)
{
    backupAndClear();
    QCoreApplication app(argc, argv);
    TestSettings tc;
    const int rc = QTest::qExec(&tc, argc, argv);
    restore();
    return rc;
}

#include "test_Settings.moc"
