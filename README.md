# avply

高速でシンプルに動画・音声ファイルを再生・トリミング・変換するメディアプレイヤー。
鑑賞用ではなく、会議録画の見直しなど実務用途を主眼に置く。
起動が速く、再生速度変更・ノーマライズ・音声明瞭化など視聴効率を高める機能を備える。

![avply スクリーンショット](docs/images/screenshot.png)

## 機能

- **高速起動**  
軽量な Qt Widgets ベースの最小構成で、起動から再生可能になるまでが速い。
- **メディア再生**  
動画・音声ファイルを読み込んで再生する。音声ファイル時はプレビュー領域を省略したコンパクト UI に切り替わる。
    - 動画：mp4, mkv, mov, avi, webm
    - 音声：mp3, wav, flac, ogg, opus
- **シークバープレビュー**  
シークバー上のマウスホバーで、その位置のフレームサムネイルと再生時刻を MPC-HC 風のポップアップで表示する。
- **会議録画向け再生支援**  
    - **再生速度変更**：ピッチ補正付きで等速〜数倍速まで切替可能（ファイル切替後も保持）
    - **ノーマライズ**：RMS コンプレッサで大声と小声の音量差をリアルタイムに圧縮
    - **音声明瞭化**：Biquad EQ 3 段カスケード（HPF 100Hz + プレゼンスブースト 3kHz + 高域シェルフ 8kHz）でこもった声を聞き取りやすくする
- **高速トリム**  
範囲指定した区間を再エンコードせずキーフレーム単位で切り出す。入力と同じコーデック・コンテナのままディスクコピー速度近くで保存する。
- **変換**  
動画は NVIDIA GPU（NVENC）で AV1 + Opus 96kbps、音声は libopus 96kbps へ再エンコードする。QWXGA（2048px）超の映像は縦横比を維持して自動縮小する。

## 動作要件

- Windows 11
- [ffmpeg](https://www.gyan.dev/ffmpeg/builds/)（別途インストール）
- NVIDIA GPU（**動画を「変換」する場合のみ必要。** AV1 NVENC 対応 / RTX 30 シリーズ以降推奨。音声のみの変換は CPU の libopus で動作するため不要）

## インストール方法

### Scoop 経由（推奨）

```powershell
scoop bucket add nikai https://github.com/aviscaerulea/scoop-bucket
scoop install nikai/avply
```

依存パッケージとして ffmpeg も自動でインストールされる。

### 手動インストール

[Releases](https://github.com/aviscaerulea/avply/releases) から `avply-<version>-x64.zip` をダウンロードして展開し、`avply.exe` を起動する。
ffmpeg は別途インストールが必要だ。（`scoop install ffmpeg` または [公式ビルド](https://www.gyan.dev/ffmpeg/builds/)）

### ffmpeg パスの解決順

avply は以下の優先順で ffmpeg を解決する。

1. `avply.toml` / `avply.local.toml` の `[ffmpeg].path` で明示指定
2. scoop の既定パス `%USERPROFILE%/scoop/apps/ffmpeg/current/bin/ffmpeg.exe`
3. `PATH` 環境変数から `ffmpeg.exe` を解決

scoop または `PATH` 配下に ffmpeg があれば設定不要で動作する。
明示指定する場合は以下のように記述する。

```toml
[ffmpeg]
path = "C:/Users/yourname/scoop/apps/ffmpeg/current/bin/ffmpeg.exe"
```

PC 固有のパスを管理から外したい場合は `avply.local.toml` に同キーを記述する。（後勝ち、VCS 管理外）

## 使用方法

### ファイルの読み込み

以下のいずれかの方法で読み込める。

- 右クリック →「ファイルを開く」を選択（読込済みなら同フォルダ、未読込ならホームフォルダを初期表示）
- アプリウィンドウへドラッグ＆ドロップ
- `avply.exe` 自体へのドラッグ＆ドロップ、Windows の「送る」、「プログラムを指定して開く」
- コマンドラインから `avply.exe <メディアファイル>` の形式で起動

読み込み中のファイル名はウィンドウタイトル（`avply - ファイル名.拡張子`）に表示する。
同じファイルを再度読み込むと先頭から再生し直す。

### キー / マウス操作一覧

| 機能 | 操作 | キー / マウス |
|------|------|---------------|
| 再生 | 再生 / 停止 | スペース、またはプレビュー領域クリック（音声時はスペースのみ） |
| 再生 | シーク | ← → / シークバードラッグ / シークバー・プレビュー領域のホイール |
| 再生 | 再生速度 ±0.05 倍 | ↑ ↓ / Ctrl+ホイール |
| 再生 | 音量 ±0.05 | Shift+↑ ↓ / Shift+ホイール |
| 再生 | ノーマライズ強度切替（Off → 小 → 中 → 大 → Off） | N |
| 再生 | 音声明瞭化強度切替（Off → 小 → 中 → 大 → Off） | V |
| 再生 | 再生条件一括リセット | g |
| トリム | 区間開始位置を指定 | 【 ボタン / `[` |
| トリム | 区間終了位置を指定 | 】 ボタン / `]` |
| トリム | 区間のみクリア（再生位置は維持） | R |
| トリム | トリム実行 / 中断 | ✂ ボタン |

### 再生

再生速度はファイルを切り替えても保持される。
g キーは 1 回目で中立値（速度 1.00 / 音量 100% / Normalize Off / Clarity Off）、2 回目で起動時のデフォルト値へ復元する。リセット状態のまま手動で各値を変更すると、次の g 押下は再び「一括リセット」として動作する。

ステータスバーには現在の再生速度・音量に加え、ノーマライズ・明瞭化の強度を常時表示する（`Normalize:0〜3` / `Clarity:0〜3`、0=Off、1=小、2=中、3=大）。

### トリム

区間を指定してからトリム実行する。シークバー上の指定区間は赤でハイライト表示される。
再エンコードしないため、開始位置はキーフレームに丸められる。Ogg/Opus など一部コンテナでは `-c copy` の仕様上、カット直後の再生位置が数十ミリ秒ズレることがある。

### 変換

右クリック →「ファイルを変換する」で実行する。動画は AV1 + Opus、音声は libopus 96kbps のみへ再エンコードする。

### 出力ファイル

入力ファイルと同じフォルダに `<元ファイル名>_clip.<拡張子>` として出力される。

| モード | 入力 | 出力拡張子 |
|--------|------|-----------|
| 変換 | 動画 | `.mp4`（AV1 + Opus） |
| 変換 | 音声 | `.opus`（libopus） |
| トリム | 動画・音声 | 入力と同じ |

### その他

右クリックメニューの設定サブメニューから、再生中の topmost・シングルインスタンス強制・プロセス優先度を変更できる（レジストリに保存）。

### 設定ファイル

`avply.toml`（ローカル上書きは `avply.local.toml`）で動作をカスタマイズできる。

#### シーク量

カーソルキーおよびマウスホイールのスキップ量を変更できる。

```toml
[seek]
left_ms          = 5000   # 左キーで戻るミリ秒数
right_ms         = 5000   # 右キーで進むミリ秒数
wheel_forward_ms = 5000   # ホイール前転（上スクロール）で進むミリ秒数
wheel_back_ms    = 5000   # ホイール後転（下スクロール）で戻るミリ秒数
```

#### 音量

再生音量の初期値を指定できる（既定 1.00、範囲 0.00〜1.00）。

```toml
[audio]
volume = 0.80   # 1.00 = 100%、0.80 = 80%
```

#### ノーマライズ

強度別の DSP パラメータを指定できる。threshold は -60.0〜0.0 dBFS、makeup gain は 0.0〜24.0 dB にクランプする。

```toml
[normalizer]
threshold_db_small  = -20.0   # 圧縮開始の RMS 閾値（小、dBFS）
threshold_db_medium = -25.0   # 同（中）
threshold_db_large  = -30.0   # 同（大）
makeup_db_small  =   5.0      # コンプレッサ後の底上げゲイン（小、dB）
makeup_db_medium =  10.0      # 同（中）
makeup_db_large  =  13.0      # 同（大）
```

#### 音声明瞭化

強度別の DSP パラメータを指定できる。peak / shelf いずれも 0.0〜12.0 dB にクランプする。

```toml
[voice_clarity]
peak_db_small  =  3.0   # 3kHz プレゼンスブースト（小、dB）
peak_db_medium =  5.0   # 同（中）
peak_db_large  =  7.0   # 同（大）
shelf_db_small  = 1.0   # 8kHz 高域シェルフブースト（小、dB）
shelf_db_medium = 2.0   # 同（中）
shelf_db_large  = 3.0   # 同（大）
```

#### サイレンストーン

`[audio].silence_tone_*` で BT ヘッドセットのアイドル復帰時プチノイズ抑制用の常時不可聴トーン出力を制御できる。スピーカー環境や有線 DAC では `silence_tone_enabled = false` で停止できる。デバイス変更時は自動で出力先を切り替える。

```toml
[audio]
silence_tone_enabled = true     # false で完全に停止する
silence_tone_freq_hz = 1000.0   # 周波数 Hz（20〜20000、既定 1 kHz は BT コーデック確実通過帯）
silence_tone_amp     = 0.0001   # 振幅 0.0〜0.01（既定 0.0001 ≒ -80 dBFS）
```

#### ウィンドウサイズ

動画読込時の初期ウィンドウサイズ上限を変更できる（デフォルト 0.7 = モニタ作業領域の 70%）。

```toml
[window]
initial_screen_ratio = 0.7
```

#### 再生速度

動画読込時の初期再生速度を指定できる（既定 1.00、上下キー操作で都度 0.05 単位の調整が可能）。

```toml
[playback]
speed = 1.00   # 1.00 = 等速、1.25 = 1.25 倍速
```

#### ハードウェアデコード

再生プレビューとシークバーホバー時のサムネイル抽出における HW デコード経路を制御できる。デフォルトは Windows 共通の `d3d11va` を最優先、フォールバックに NVIDIA NVDEC（`cuda`）を指定する。

GPU が無い環境やドライバ非対応の場合、Qt および ffmpeg は自動的に CPU デコードへフォールバックするため、未調整でも動作する。

```toml
[playback]
hw_decoder_priority = "d3d11va,cuda"   # QMediaPlayer の HW デコーダ優先順位（カンマ区切り、空文字で Qt 自動選択）
thumbnail_hwaccel   = "auto"           # サムネイル抽出の ffmpeg -hwaccel 値（"none" でスキップ）
```

## ビルド方法

### 必要ツール

- Visual Studio 2022+ Build Tools（C++ ワークロード）
- CMake 3.25+（`scoop install cmake`）
- Qt 6.10.3 MSVC2022 x64
  - `python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --outputdir <インストールフォルダ> --modules qtmultimedia`
  - インストールフォルダは `CMakePresets.json` の `CMAKE_PREFIX_PATH` に合わせること

### ビルド手順

```powershell
pwsh.exe -File build.ps1
```

実行ファイルは `out/Release/avply.exe` に生成される。

## 技術仕様

- 言語：C++17
- GUI フレームワーク：Qt 6.10 Widgets + Multimedia（LGPLv3、DLL 動的リンク）
- 音声時間圧縮：SoundTouch 2.4.0（LGPL v2.1+、静的リンク）
- 映像コーデック：AV1（av1_nvenc, VBR CQ 28, preset p6）
- 音声コーデック：Opus 96kbps（libopus）
- ハードウェアアクセラレーション：NVIDIA NVENC（CUDA、動画変換時のみ使用）
- 外部ツール連携：ffmpeg / ffprobe（QProcess 経由）

## ライセンス

avply 本体は **GNU LGPL v3** で配布する。全文は同梱の `LICENSE`（LGPL v3）および `COPYING`（GPL v3）を参照。

依存ライブラリのライセンス対応：

- Qt 6.10（LGPLv3）：windeployqt が配布する DLL を動的リンクする。利用者は同名 DLL を差し替えることで Qt を入れ替えられる
- SoundTouch 2.4.0（LGPL v2.1+）：静的リンクのため、本体ライセンスを LGPL v3 とすることで再リンク権を確保する
- ffmpeg：QProcess による外部プロセス呼び出しのため、リンク関係は発生しない
