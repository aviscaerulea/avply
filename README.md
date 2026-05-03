# vcutter

会議録画などの動画ファイルをシンプルにカット・変換するための Windows 11 GUI ツール。

## 機能

- **動画プレビュー**：音声付きで再生可能。シークバーまたはカーソルキー（左右）で任意の位置に移動できる
- **ドラッグ＆ドロップ対応**：動画ファイルをウィンドウやプレビュー領域へ落として読み込める
- **範囲カット**：開始/終了マーカーで保持する区間を指定。シークバー上に赤でハイライト表示される
- **AV1 変換**：NVIDIA GPU（NVENC）を使用して AV1 + Opus 96kbps で出力
- **自動リサイズ**：FHD（1920px）を超える映像は縦横比を維持してスケールダウン

## 動作要件

- Windows 11
- NVIDIA GPU（AV1 NVENC 対応 / RTX 30 シリーズ以降推奨）
- [ffmpeg](https://www.gyan.dev/ffmpeg/builds/)（別途インストール）

## インストール方法

ffmpeg のインストール（未インストールの場合）：

```powershell
scoop install ffmpeg
```

`out/Release/vcutter.exe` を起動する。

ffmpeg.exe のパスは実行ファイルと同階層の `vcutter.toml` で設定する。
scoop でインストールした場合はパス未設定でも自動検出される。

```toml
[ffmpeg]
path = "C:/Users/yourname/scoop/apps/ffmpeg/current/bin/ffmpeg.exe"
```

PC 固有のパスを管理から外したい場合は `vcutter.local.toml` に同キーを記述する（後勝ち、VCS 管理外）。

## 使用方法

1. 「開く...」から動画ファイル（mp4 / mkv / mov / avi / webm）を選択するか、ウィンドウへドロップする
2. プレビュー領域をクリックすると音声付きで再生/停止できる
3. シークバーのドラッグ、またはカーソルキー（← / →）で再生位置を移動する（デフォルト 3 秒スキップ）
4. 開始位置で「開始を設定」、終了位置で「終了を設定」をクリックして区間を決める
5. 「変換」をクリックして AV1 変換を開始する（変換中は同じボタンが「中止」に切り替わる）
6. 入力ファイルと同じフォルダに `<元ファイル名>_cut.mp4` として出力される

カーソルキーのスキップ量は `vcutter.toml` の `[seek]` セクションで変更できる。

```toml
[seek]
left_ms  = 3000   # 左キーで戻るミリ秒数
right_ms = 3000   # 右キーで進むミリ秒数
```

## ビルド方法

### 必要ツール

- Visual Studio 2022+ Build Tools（C++ ワークロード）
- CMake 3.25+（`scoop install cmake`）
- Qt 6.8.3 MSVC2022 x64（`python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir D:/Qt`）

### ビルド手順

```powershell
pwsh.exe -File build.ps1
```

実行ファイルは `out/Release/vcutter.exe` に生成される。

## 技術仕様

- 言語：C++17
- GUI フレームワーク：Qt 6.8 Widgets + Multimedia（LGPLv3）
- 映像コーデック：AV1（av1_nvenc, VBR CQ 35, preset p6）
- 音声コーデック：Opus 96kbps
- ハードウェアアクセラレーション：NVIDIA NVENC（CUDA）
- 外部ツール連携：ffmpeg / ffprobe（QProcess 経由）
