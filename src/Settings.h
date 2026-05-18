#pragma once
#include <QSettings>

// HKEY_CURRENT_USER\Software\avply\avply 配下に保存する UI 由来の永続設定
// QApplication::setOrganizationName("avply")／setApplicationName("avply") を前提とする。
// TOML の設定（avply.toml）は責務を分離し、こちらでは扱わない
class Settings {
public:
    static Settings& instance();

    // 再生中ウィンドウを最前面に固定するか
    bool topmostWhilePlaying() const;
    void setTopmostWhilePlaying(bool value);

    // 単一インスタンス強制（2 個目は引数転送して終了）
    bool singleInstance() const;
    void setSingleInstance(bool value);

    // プロセス優先度を ABOVE_NORMAL に設定するか
    bool aboveNormalPriority() const;
    void setAboveNormalPriority(bool value);

    // 再生時に RMS コンプレッサによるノーマライズを有効にするか（デフォルト true）
    bool normalizeEnabled() const;
    void setNormalizeEnabled(bool value);

    // 再生時の音声明瞭化（Biquad EQ）強度
    // 0=Off / 1=Small / 2=Medium / 3=Large（デフォルト 2=Medium）
    // 値は VoiceClarity::Level と 1:1 対応する。enum を直接公開しないのは
    // QSettings の永続化が int で完結するため、依存の方向性を Settings → VoiceClarity 単方向に保つため
    int  voiceClarityLevel() const;
    void setVoiceClarityLevel(int value);

private:
    Settings();

    // setValue 直後に sync() で flush し、レジストリへ即時反映する
    void writeBool(const char* key, bool value);
    bool readBool(const char* key, bool defaultValue) const;
    void writeInt(const char* key, int value);
    int  readInt(const char* key, int defaultValue) const;

    mutable QSettings m_settings;
};
