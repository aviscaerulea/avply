#pragma once
#include <QString>
#include <QColor>

// アプリケーション設定
struct AppConfig {
    QString ffmpegPath;

    // カーソルキーシーク量（ミリ秒）
    int seekLeftMs  = 5000;
    int seekRightMs = 5000;

    // Shift+カーソルキーの大シーク量（ミリ秒）
    // 0 以下でその方向の大シーク無効
    int seekShiftLeftMs  = 20000;
    int seekShiftRightMs = 20000;

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
    // 周波数は 20〜20000Hz、振幅は 0.0〜0.01（-40dBFS）にクランプする。
    // 既定 1kHz / 0.0001（約 -80dBFS）は BT コーデックのパスバンド内かつ
    // 通常環境では知覚困難なレベル。設定ミスによる耳障りな音量を防ぐため
    // 上限を 0.01 と低めに固定し、16bit フルスケール（1.0）の指定はできない。
    double silenceToneFreqHz = 1000.0;
    double silenceToneAmp    = 0.0001;

    // 字幕生成に使う ggml モデル
    // ファイル名だけなら subtitleModelUrl と連結した URL から自動ダウンロードし、
    // 実行ファイルと同階層の model/ へ置く。絶対パスならその実体を使い、自動取得はしない
    QString subtitleModel    = "ggml-large-v3-turbo-q5_0.bin";
    QString subtitleModelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/";

    // whisper に渡す言語コード。既定は日本語
    QString subtitleLanguage = "ja";

    // whisper へ渡す事前文脈（initial_prompt）
    // 固有名詞や専門用語を並べると認識がその語へ寄る。強制ではなく偏りを与えるだけで、
    // 書いた語が必ず出る保証はない。空なら何も渡さない。
    // whisper のテキスト文脈長の半分（224 トークン）を超えた分は whisper 側が捨てる
    QString subtitlePrompt;

    // 字幕の文字色と背景色（avply.toml では "#RRGGBB" で指定する）
    // 空指定と解釈できない値は既定値のままにする
    QColor subtitleTextColor       = QColor(0xFF, 0xFF, 0xFF);
    QColor subtitleBackgroundColor = QColor(0x00, 0x00, 0x00);

    // 字幕背景の不透明度（0.0 で透明、1.0 で塗りつぶし）。0.0〜1.0 にクランプする。
    // 既定の 0.80 は、白地の多い映像でも文字が背景に埋もれないように旧固定値（約 0.63）から上げた値だ
    double subtitleBackgroundOpacity = 0.80;
};

// avply.toml / avply.local.toml から設定を読み込むユーティリティ
namespace Config {
    // 実行ファイルと同階層の avply.toml を読み、avply.local.toml が
    // 存在すれば同キーを後勝ちで上書きする。
    // [ffmpeg].path の未設定時は scoop デフォルトパス → PATH 解決の順にフォールバックする。
    AppConfig load();

    // 実行ファイルのあるディレクトリ絶対パス
    // QCoreApplication 未構築でも動くよう Win32 API を直接使う。
    // MAX_PATH 超のロングパス環境にも対応するためバッファを動的拡張する。
    // 取得失敗時は空文字を返す
    QString exeDirectory();
} // namespace Config
