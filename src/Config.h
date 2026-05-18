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

    // ノーマライズ強度別の DSP パラメータ
    // 強度（Small/Medium/Large）ごとに threshold（圧縮開始閾値 dBFS）と makeup（底上げ dB）を
    // 独立指定する。Ratio / Attack / Release / Limiter は強度共通のためここでは扱わない。
    // threshold は -60.0〜0.0、makeup は 0.0〜24.0 にクランプする
    double normalizerThresholdDbSmall  = -20.0;
    double normalizerThresholdDbMedium = -25.0;
    double normalizerThresholdDbLarge  = -30.0;
    double normalizerMakeupDbSmall  =   5.0;
    double normalizerMakeupDbMedium =  10.0;
    double normalizerMakeupDbLarge  =  13.0;

    // 音声明瞭化（Biquad EQ）強度別の DSP パラメータ
    // 強度（Small/Medium/Large）ごとに peakDb（3kHz プレゼンスブースト）と shelfDb（8kHz 高域シェルフ）を
    // 独立指定する。HPF / フィルタ周波数 / Q は強度共通のためここでは扱わない。
    // peakDb / shelfDb はいずれも 0.0〜12.0 dB にクランプする（負値は明瞭化の趣旨に反する）
    double voiceClarityPeakDbSmall  = 3.0;
    double voiceClarityPeakDbMedium = 5.0;
    double voiceClarityPeakDbLarge  = 7.0;
    double voiceClarityShelfDbSmall  = 1.0;
    double voiceClarityShelfDbMedium = 2.0;
    double voiceClarityShelfDbLarge  = 3.0;

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
