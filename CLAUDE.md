# vcutter — CLAUDE.md

## 開発環境

| 項目 | バージョン |
|------|-----------|
| コンパイラ | MSVC 14.50（VS 2026 Build Tools, x64） |
| CMake | 4.3.2 以上（scoop） |
| Qt | 6.8.3 MSVC2022 x64（インストール先は `CLAUDE.local.md` 参照） |
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
  VideoView.h/cpp     QMediaPlayer + QVideoWidget による音声付きプレビュー
  RangeSlider.h/cpp   開始〜終了区間を赤系でハイライト表示するスライダー
  Encoder.h/cpp       変換実行・進捗通知
  OutputNamer.h/cpp   出力ファイル名生成（{base}_cut.mp4 形式）
  Config.h/cpp        vcutter.toml 読み込み（ffmpeg_path 等）
```

## 実装上の注意点

### ビルド環境の注意

- `windeployqt` が「VCINSTALLDIR is not set」警告を出すが無害。DLL は正常に配置される

### エンコード仕様

`convert-mp4` skill（`~/.claude/skills/convert-mp4/scripts/convert-mp4.sh`）と同一仕様。

- av1_nvenc, rc=vbr, cq=35, preset=p6
- libopus 96kbps
- FHD（幅 1920px）超の場合は `scale=1920:-2`

### Encoder の生成タイミング

`Encoder` は `onConvert` のタイミングで生成し、ffmpeg パスを渡す。
コンストラクタでは生成しない（`m_encoder = nullptr` 初期値）。

### ffmpeg パス設定

UI からの編集はせず、実行ファイルと同階層の `vcutter.toml`（ローカル上書きは `vcutter.local.toml`）の `[ffmpeg].path` で指定する。
未設定時は `%USERPROFILE%/scoop/apps/ffmpeg/current/bin/ffmpeg.exe` にフォールバックする。

### カーソルキーシーク設定

`vcutter.toml` の `[seek]` セクションで左右カーソルキーのスキップ量（ms）を指定する。
デフォルトは左右ともに 3000 ms。0 以下に設定するとそのキーのシークが無効になる。

### 出力ファイル名

入力ファイルと同一フォルダに `<元ファイルベース名>_cut.mp4` で出力する。
同名が既に存在する場合は `_cut_2.mp4`、`_cut_3.mp4` の順で衝突回避する。

## 参考

- @README.md
