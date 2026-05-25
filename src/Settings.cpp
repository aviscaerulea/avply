#include "Settings.h"

namespace {
constexpr const char* kKeyTopmost        = "topmostWhilePlaying";
constexpr const char* kKeySingleInstance = "singleInstance";
constexpr const char* kKeyPriority       = "aboveNormalPriority";
constexpr const char* kKeySpeechEnhance  = "speechEnhanceLevel";

// 音声強調の強度範囲（SpeechEnhancer::Level と対応）
// 0=Off / 1=Low / 2=Medium / 3=High。Settings 単独でクランプするため数値定義する
constexpr int kSpeechEnhanceMin     = 0;
constexpr int kSpeechEnhanceMax     = 3;
constexpr int kSpeechEnhanceDefault = 2;
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

int Settings::speechEnhanceLevel() const
{
    const int v = readInt(kKeySpeechEnhance, kSpeechEnhanceDefault);
    if (v < kSpeechEnhanceMin) return kSpeechEnhanceMin;
    if (v > kSpeechEnhanceMax) return kSpeechEnhanceMax;
    return v;
}

void Settings::setSpeechEnhanceLevel(int value)
{
    if (value < kSpeechEnhanceMin) value = kSpeechEnhanceMin;
    if (value > kSpeechEnhanceMax) value = kSpeechEnhanceMax;
    // 同値書込は QSettings::sync の不要なディスク I/O を発生させるため早期 return する。
    // applyPlaybackState（g キー一括リセット）からのノーオプ呼び出しでも writeInt が走るのを抑止する
    if (speechEnhanceLevel() == value) return;
    writeInt(kKeySpeechEnhance, value);
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
