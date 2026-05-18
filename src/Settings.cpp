#include "Settings.h"

namespace {
constexpr const char* kKeyTopmost        = "topmostWhilePlaying";
constexpr const char* kKeySingleInstance = "singleInstance";
constexpr const char* kKeyPriority       = "aboveNormalPriority";
constexpr const char* kKeyNormalize      = "normalizeLevel";
constexpr const char* kKeyVoiceClarity   = "voiceClarityLevel";

// ノーマライズ・音声明瞭化の強度範囲（それぞれ Normalizer::Level / VoiceClarity::Level と対応）
// 0=Off / 1=Small / 2=Medium / 3=Large。Settings 単独でクランプするため数値定義する
constexpr int kNormalizeMin     = 0;
constexpr int kNormalizeMax     = 3;
constexpr int kNormalizeDefault = 2;
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

int Settings::normalizeLevel() const
{
    const int v = readInt(kKeyNormalize, kNormalizeDefault);
    if (v < kNormalizeMin) return kNormalizeMin;
    if (v > kNormalizeMax) return kNormalizeMax;
    return v;
}

void Settings::setNormalizeLevel(int value)
{
    if (value < kNormalizeMin) value = kNormalizeMin;
    if (value > kNormalizeMax) value = kNormalizeMax;
    // 同値書込は QSettings::sync の不要なディスク I/O を発生させるため早期 return する。
    // applyPlaybackState（g キー一括リセット）からのノーオプ呼び出しでも writeInt が走るのを抑止する
    if (normalizeLevel() == value) return;
    writeInt(kKeyNormalize, value);
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
    // 同値書込のディスク I/O を抑止する（setNormalizeLevel と同じ理由）
    if (voiceClarityLevel() == value) return;
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
