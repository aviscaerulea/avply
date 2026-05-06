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

private:
    Settings();

    // setValue 直後に sync() で flush し、レジストリへ即時反映する
    void writeBool(const char* key, bool value);
    bool readBool(const char* key, bool defaultValue) const;

    mutable QSettings m_settings;
};
