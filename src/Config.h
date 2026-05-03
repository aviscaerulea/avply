#pragma once
#include <QString>

// アプリケーション設定
struct AppConfig {
    QString ffmpegPath;

    // カーソルキーシーク量（ミリ秒）
    int seekLeftMs  = 3000;
    int seekRightMs = 3000;
};

// vcutter.toml / vcutter.local.toml から設定を読み込むユーティリティ
namespace Config {
    // 実行ファイルと同階層の vcutter.toml を読み、vcutter.local.toml が
    // 存在すれば同キーを後勝ちで上書きする。
    // ffmpeg_path 未設定時は scoop デフォルトパスにフォールバックする。
    AppConfig load();
} // namespace Config
