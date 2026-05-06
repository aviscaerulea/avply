# avply — CLAUDE.md

## 開発環境

| 項目 | バージョン |
|------|-----------|
| コンパイラ | MSVC 14.50（VS 2026 Build Tools, x64） |
| CMake | 4.3.2 以上（scoop） |
| Qt | 6.10.3 MSVC2022 x64（インストール先は `CLAUDE.local.md` 参照） |
| ffmpeg | scoop インストール推奨 |
| ビルドプリセット | `msvc-release`（`CMakePresets.json` 参照） |

## ビルド方法

```powershell
# VS 環境ロード + ビルド（推奨）
pwsh.exe -File build.ps1

# 手動
cmake --preset msvc-release
cmake --build --preset msvc-release
```

`CMakePresets.json` の `CMAKE_PREFIX_PATH` に Qt のインストールパスを設定すること。
ローカル固有のパスは `CLAUDE.local.md` に記載している。

cmake が PATH 未追加の環境では `scoop install cmake` で追加する。

## プロジェクト構成

```
src/
  main.cpp            エントリポイント
  MainWindow.h/cpp    UI・イベント処理（D&D 含む）
  FfmpegRunner.h/cpp  ffprobe/ffmpeg 実行ユーティリティ
  VideoView.h/cpp     QMediaPlayer + QQuickView (VideoOutput) + QAudioOutput による音声付きプレビュー
  VideoOutput.qml     プレビュー領域の QML（クリック・右クリック・ホイール受付）
  RangeSlider.h/cpp   開始〜終了区間を赤系でハイライト表示するスライダー
  Encoder.h/cpp       変換実行・進捗通知
  OutputNamer.h/cpp   出力ファイル名生成（{base}_clip.mp4 形式）
  Config.h/cpp        avply.toml 読み込み（ffmpeg_path 等）
```

## 実装上の注意点

### ビルド環境の注意

- `windeployqt` が「VCINSTALLDIR is not set」警告を出すが無害。DLL は正常に配置される

### 受け入れ可能ファイル

- 動画：mp4 / mkv / mov / avi / webm
- 音声：mp3 / wav / flac / ogg / opus

「動画か音声のみか」は ffprobe 結果（`VideoInfo.codec` と `width`）から判定する（`MainWindow::isAudioOnly()`）。
拡張子ではなく中身で分岐するため、コンテナと中身が一致しないケース（例：mkv 内が音声のみ）にも追従する。

### エンコード仕様

変換（再エンコード）：

- 動画あり：
    - av1_nvenc, rc=vbr, cq=28, preset=p6
    - GOP 120 フレーム（30fps 想定で 4 秒間隔のキーフレーム）
    - spatial_aq 有効（フラット領域・テキストのビット配分改善）
    - libopus 96kbps
    - QWXGA（幅 2048px）超の場合は `scale=2048:-2`
    - `-hwaccel cuda` で HW デコードを有効化
- 音声のみ：
    - `-vn` で映像ストリーム除外
    - libopus 96kbps のみ
    - NVENC は不要（CPU エンコード）

トリム（ストリームコピー）：

- `-c copy` でキーフレーム単位カット
- 解像度・コーデック・ビットレートは入力をそのまま維持
- 出力コンテナは入力と同じ拡張子を維持する

### Encoder の生成タイミング

`Encoder` は `onConvert` のタイミングで生成し、ffmpeg パスを渡す。
コンストラクタでは生成しない（`m_encoder = nullptr` 初期値）。

### ffmpeg パス設定

UI からの編集はせず、実行ファイルと同階層の `avply.toml`（ローカル上書きは `avply.local.toml`）の `[ffmpeg].path` で指定する。
未設定時は以下の順でフォールバックする。

1. scoop 既定パス `%USERPROFILE%/scoop/apps/ffmpeg/current/bin/ffmpeg.exe`
2. `QStandardPaths::findExecutable("ffmpeg")` による `PATH` 解決

### カーソルキーシーク設定

`avply.toml` の `[seek]` セクションで左右カーソルキーおよびマウスホイールのスキップ量（ms）を指定する。
デフォルトはいずれも 5000 ms。0 以下に設定するとそのキー／方向のシークが無効になる。

### 音量設定

`avply.toml` の `[audio].volume` で再生音量の初期値を指定する。
デフォルトは 1.00、範囲は 0.00〜1.00 にクランプする（`QAudioOutput::setVolume` の有効範囲）。
再生中は Shift+カーソルキー（上下）で ±0.05 ずつ調整できる（`MainWindow::changeVolume`）。

100% 超のソフトウェアブーストはサポートしない。
過去に `QAudioBufferOutput` + 専用スレッド `QAudioSink` でブースト経路を実装したが、
gain × playbackRate 高負荷時に resampler overshoot 起因のノイズを完全に解消できず撤去した。
音量底上げが必要な場合はソース素材側でノーマライズすること。

### 出力ファイル名

入力ファイルと同一フォルダに `<元ファイルベース名>_clip.<拡張子>` で出力する。
同名が既に存在する場合は `_clip_2.<拡張子>`、`_clip_3.<拡張子>` の順で衝突回避する。

拡張子はモードと入力種別で決定する。

| モード | 入力 | 出力拡張子 |
|--------|------|-----------|
| 変換 | 動画 | `.mp4`（AV1 + Opus） |
| 変換 | 音声のみ | `.opus`（libopus） |
| トリム | 動画・音声どちらも | 入力と同じ拡張子 |

## 参考

- @README.md
