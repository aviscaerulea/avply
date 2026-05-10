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

    // QMediaPlayer FFmpeg バックエンドの HW デコード優先順位
    // QT_FFMPEG_DECODING_HW_DEVICE_TYPES と同形式（カンマ区切り）。
    // 空文字なら Qt の自動選択に任せる
    QString hwDecoderPriority = "d3d11va,cuda";

    // ThumbnailExtractor が ffmpeg に渡す -hwaccel 値
    // "auto" / "d3d11va" / "cuda" 等。"none" は -hwaccel 指定をスキップする
    QString thumbnailHwaccel = "auto";

    // BT 機器のアイドル復帰時プチノイズ抑制用サイレンストーンの有効化
    // false にすると SilenceTone を起動せず、OS への常時音声出力を行わない
    bool silenceToneEnabled = true;

    // サイレンストーンの周波数（Hz）と振幅
    // 周波数は 20〜20000 Hz、振幅は 0.0〜0.01（-40 dBFS）にクランプする。
    // 既定 1 kHz / 0.0001（約 -80 dBFS）は BT コーデックのパスバンド内かつ
    // 通常環境では知覚困難なレベル。設定ミスによる耳障りな音量を防ぐため
    // 上限を 0.01 と低めに固定し、16bit フルスケール（1.0）の指定はできない。
    double silenceToneFreqHz = 1000.0;
    double silenceToneAmp    = 0.0001;
};

// avply.toml / avply.local.toml から設定を読み込むユーティリティ
namespace Config {
    // 実行ファイルと同階層の avply.toml を読み、avply.local.toml が
    // 存在すれば同キーを後勝ちで上書きする。
    // ffmpeg_path 未設定時は scoop デフォルトパスにフォールバックする。
    AppConfig load();
} // namespace Config
