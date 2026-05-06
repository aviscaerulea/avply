#include "Settings.h"

namespace {
constexpr const char* kKeyTopmost        = "topmostWhilePlaying";
constexpr const char* kKeySingleInstance = "singleInstance";
constexpr const char* kKeyPriority       = "aboveNormalPriority";
} // namespace

Settings::Settings()
    : m_settings(QSettings::NativeFormat, QSettings::UserScope, "avply", "avply")
{
    // QApplication 構築前にも参照されるため、QSettings に組織名・アプリ名を明示して構築する
    // QApplication::setOrganizationName 経由の引数なしコンストラクタは使えない
}

Settings& Settings::instance()
{
    static Settings s;
    return s;
}

bool Settings::topmostWhilePlaying() const
{
    return readBool(kKeyTopmost, false);
}

void Settings::setTopmostWhilePlaying(bool value)
{
    writeBool(kKeyTopmost, value);
}

bool Settings::singleInstance() const
{
    return readBool(kKeySingleInstance, false);
}

void Settings::setSingleInstance(bool value)
{
    writeBool(kKeySingleInstance, value);
}

bool Settings::aboveNormalPriority() const
{
    return readBool(kKeyPriority, false);
}

void Settings::setAboveNormalPriority(bool value)
{
    writeBool(kKeyPriority, value);
}

bool Settings::readBool(const char* key, bool defaultValue) const
{
    return m_settings.value(key, defaultValue).toBool();
}

void Settings::writeBool(const char* key, bool value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
}
