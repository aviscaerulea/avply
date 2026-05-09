#pragma once
#include <QString>

// アプリケーション設定
struct AppConfig {
    QString ffmpegPath;

    // カーソルキーシーク量（ミリ秒）
    int seekLeftMs  = 5000;
    int seekRightMs = 5000;

    // マウスホイールシーク量（ミリ秒）
    // 0 以下でそのホイール方向のシーク無効
    int wheelForwardMs = 5000;
    int wheelBackMs    = 5000;

    // 動画読込時の初期再生速度（1.00 で等速）
    double playbackSpeed = 1.0;

    // 再生音量
    // 1.00 = 100%、0.50 = 50%。0.00〜1.00 にクランプ
    double audioVolume = 1.0;

    // 動画読込時の初期ウィンドウサイズ上限のモニタ比率（0.1〜1.0、デフォルト 0.7）
    double initialScreenRatio = 0.7;
};

// avply.toml / avply.local.toml から設定を読み込むユーティリティ
namespace Config {
    // 実行ファイルと同階層の avply.toml を読み、avply.local.toml が
    // 存在すれば同キーを後勝ちで上書きする。
    // ffmpeg_path 未設定時は scoop デフォルトパスにフォールバックする。
    AppConfig load();
} // namespace Config
