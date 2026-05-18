#include "Settings.h"

namespace {
constexpr const char* kKeyTopmost        = "topmostWhilePlaying";
constexpr const char* kKeySingleInstance = "singleInstance";
constexpr const char* kKeyPriority       = "aboveNormalPriority";
constexpr const char* kKeyNormalize      = "normalizeEnabled";
constexpr const char* kKeyVoiceClarity   = "voiceClarityLevel";

// 音声明瞭化の強度範囲（VoiceClarity::Level と対応）
// 0=Off / 1=Small / 2=Medium / 3=Large。Settings 単独でクランプするため数値定義する
constexpr int kVoiceClarityMin     = 0;
constexpr int kVoiceClarityMax     = 3;
constexpr int kVoiceClarityDefault = 2;
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

bool Settings::normalizeEnabled() const
{
    return readBool(kKeyNormalize, true);
}

void Settings::setNormalizeEnabled(bool value)
{
    writeBool(kKeyNormalize, value);
}

int Settings::voiceClarityLevel() const
{
    const int v = readInt(kKeyVoiceClarity, kVoiceClarityDefault);
    if (v < kVoiceClarityMin) return kVoiceClarityMin;
    if (v > kVoiceClarityMax) return kVoiceClarityMax;
    return v;
}

void Settings::setVoiceClarityLevel(int value)
{
    if (value < kVoiceClarityMin) value = kVoiceClarityMin;
    if (value > kVoiceClarityMax) value = kVoiceClarityMax;
    writeInt(kKeyVoiceClarity, value);
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

int Settings::readInt(const char* key, int defaultValue) const
{
    return m_settings.value(key, defaultValue).toInt();
}

void Settings::writeInt(const char* key, int value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
}
