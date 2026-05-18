# avply

動画・音声ファイルをシンプルに再生・トリミングできる Windows 11 GUI ツール。

![avply スクリーンショット](docs/images/screenshot.png)

## 機能

- **メディアプレビュー**  
音声付きで再生可能。スペースキーで再生/停止、停止ボタンで先頭に戻す。カーソルキー（左右）またはシークバー・プレビュー領域のマウスホイールでシーク、カーソルキー（上下）または Ctrl+マウスホイールで再生速度調整（±0.05 倍）、Shift+カーソルキー（上下）または Shift+マウスホイールで音量調整（±0.05、0〜100%）ができる。再生速度はファイルを読み直しても維持される。
- **シークバープレビュー**  
シークバー上でマウスをホバーすると、その位置のフレームサムネイルと再生時刻を MPC-HC 風のポップアップで表示する。音声ファイルは時刻のみ表示する。
- **音声ファイル対応**  
mp3 / wav / flac / ogg / opus を読み込むとプレビュー領域は非表示になり、シーク・トリム・変換のみのコンパクト UI に切り替わる。
- **ドラッグ＆ドロップ対応**  
メディアファイルをウィンドウやプレビュー領域へ落として読み込める。
- **コマンドライン起動**  
`avply.exe <メディアファイル>` 形式で起動可能。実行ファイルへの D&D・Windows の「送る」・「プログラムを指定して開く」からも対応。
- **範囲指定**  
「【」「】」ボタンで保持する区間の開始/終了を指定。シークバー上に赤でハイライト表示される。
- **高速トリム**  
再エンコードせずキーフレーム単位で切り出す（ハサミ ✂ アイコンのボタン）。処理はディスクコピー速度近くだが、開始位置はキーフレームに丸められる。出力拡張子は入力と同じものを維持する。Ogg/Opus など一部コンテナでは `-c copy` の仕様上、カット直後の再生位置が数十ミリ秒ズレることがある。
- **変換**  
動画は NVIDIA GPU（NVENC）で AV1 + Opus 96kbps、音声は libopus 96kbps のみで出力する。
- **自動リサイズ**  
QWXGA（2048px）を超える映像は縦横比を維持してスケールダウン。
- **ノーマライズ**  
RMS コンプレッサで大声と小声の音量差をリアルタイムに縮小する。強度は Off / 小 / 中 / 大 の 4 段階で、`N` キーを押下する度に Off → 小 → 中 → 大 → Off の順で循環する。ステータスバーに「Normalize:0〜3」を常時表示する（0=Off / 1=小 / 2=中 / 3=大）。設定はレジストリに保存され、デフォルトは中。強度別の threshold / makeup gain は `avply.toml` の `[normalizer]` セクションで調整できる。
- **音声明瞭化**  
Biquad EQ 3 段カスケード（HPF 100Hz + プレゼンスブースト 3kHz + 高域シェルフ 8kHz）で人声の子音帯域を持ち上げ、こもり気味の音声を聞き取りやすくする。強度は Off / 小 / 中 / 大 の 4 段階で、`V` キーを押下する度に Off → 小 → 中 → 大 → Off の順で循環する。ステータスバーに「Clarity:0〜3」を常時表示する（0=Off / 1=小 / 2=中 / 3=大）。設定はレジストリに保存され、デフォルトは中。強度別の peak / shelf gain は `avply.toml` の `[voice_clarity]` セクションで調整できる。
- **コンテキストメニュー**  
右クリックでファイルを開く・ファイルパスをコピー・変換・トリムを実行できる。設定サブメニューから再生中 topmost・シングルインスタンス強制・プロセス優先度を変更できる。（レジストリ保存）読込中のファイル名はウィンドウタイトル（`avply - ファイル名.拡張子`）に表示する。

## 動作要件

- Windows 11
- [ffmpeg](https://www.gyan.dev/ffmpeg/builds/)（別途インストール）
- NVIDIA GPU（**動画を「変換」する場合のみ必要。** AV1 NVENC 対応 / RTX 30 シリーズ以降推奨。音声のみの変換は CPU の libopus で動作するため不要）

## インストール方法

ffmpeg のインストール（未インストールの場合）：

```powershell
scoop install ffmpeg
```

`out/Release/avply.exe` を起動する。

ffmpeg.exe は以下の優先順で解決される。

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

1. メディアファイルを以下のいずれかの方法で読み込む
    - 対応形式：動画は mp4 / mkv / mov / avi / webm、音声は mp3 / wav / flac / ogg / opus
    - 右クリック →「ファイルを開く」を選択（読込済みなら同フォルダ、未読込ならホームフォルダを初期表示）
    - アプリウィンドウへドラッグ＆ドロップ
    - `avply.exe` 自体へのドラッグ＆ドロップ・Windows の「送る」・「プログラムを指定して開く」
2. プレビュー領域をクリックまたはスペースキーで再生/停止できる（音声ファイル時はスペースキーのみ）
3. シークバーのドラッグ、カーソルキー（← / →）、またはシークバー・プレビュー領域のマウスホイールで再生位置を移動する（デフォルト 5 秒スキップ）。カーソルキー（↑ / ↓）または Ctrl+マウスホイールで再生速度を ±0.05 倍ずつ調整、Shift+カーソルキー（↑ / ↓）または Shift+マウスホイールで音量を ±0.05 ずつ調整できる（現在速度・音量はステータスバーに表示）。`N` キーはノーマライズ（音量差縮小）の強度を、`V` キーは音声明瞭化（人声強調）の強度をそれぞれ Off → 小 → 中 → 大 → Off の順で循環させる。
4. 開始位置で「【」（または `[` キー）、終了位置で「】」（または `]` キー）で区間を決める。`R` キーで再生位置を維持したまま区間のみクリアできる
5. 変換またはトリムで処理を開始する（実行中、トリムボタンは停止記号「■」に切り替わる）
    - 「変換」：右クリック →「ファイルを変換する」を選択。動画は AV1 + Opus へ、音声は libopus 96kbps のみへ再エンコード
    - 「トリム」：ハサミ ✂ アイコンのボタンまたは右クリック →「ファイルをトリムする」を選択。再エンコードせず `-c copy` で高速切り出し（解像度・コーデック・ビットレートは入力をそのまま維持）
6. 入力ファイルと同じフォルダに `<元ファイル名>_clip.<拡張子>` として出力される
    - 変換 + 動画 → `.mp4`、変換 + 音声 → `.opus`、トリム → 入力と同じ拡張子

カーソルキーおよびマウスホイールのスキップ量は `avply.toml` の `[seek]` セクションで変更できる。

```toml
[seek]
left_ms          = 5000   # 左キーで戻るミリ秒数
right_ms         = 5000   # 右キーで進むミリ秒数
wheel_forward_ms = 5000   # ホイール前転（上スクロール）で進むミリ秒数
wheel_back_ms    = 5000   # ホイール後転（下スクロール）で戻るミリ秒数
```

再生音量の初期値は `[audio].volume` で指定できる（既定 1.00、範囲 0.00〜1.00）。再生中は Shift+カーソルキー（↑ / ↓）で ±0.05 ずつ変更できる。

```toml
[audio]
volume = 0.80   # 1.00 = 100%、0.80 = 80%
```

ノーマライズの強度別 DSP パラメータは `[normalizer]` セクションで指定できる。threshold は -60.0〜0.0 dBFS、makeup gain は 0.0〜24.0 dB にクランプする。

```toml
[normalizer]
threshold_db_small  = -20.0   # 圧縮開始の RMS 閾値（小、dBFS）
threshold_db_medium = -25.0   # 同（中）
threshold_db_large  = -30.0   # 同（大）
makeup_db_small  =   5.0      # コンプレッサ後の底上げゲイン（小、dB）
makeup_db_medium =  10.0      # 同（中）
makeup_db_large  =  13.0      # 同（大）
```

音声明瞭化の強度別 DSP パラメータは `[voice_clarity]` セクションで指定できる。peak / shelf いずれも 0.0〜12.0 dB にクランプする。

```toml
[voice_clarity]
peak_db_small  =  3.0   # 3kHz プレゼンスブースト（小、dB）
peak_db_medium =  5.0   # 同（中）
peak_db_large  =  7.0   # 同（大）
shelf_db_small  = 1.0   # 8kHz 高域シェルフブースト（小、dB）
shelf_db_medium = 2.0   # 同（中）
shelf_db_large  = 3.0   # 同（大）
```

`[audio].silence_tone_*` で BT ヘッドセットのアイドル復帰時プチノイズ抑制用の常時不可聴トーン出力を制御できる。スピーカー環境や有線 DAC では `silence_tone_enabled = false` で停止できる。デバイス変更時は自動で出力先を切り替える。

```toml
[audio]
silence_tone_enabled = true     # false で完全に停止する
silence_tone_freq_hz = 1000.0   # 周波数 Hz（20〜20000、既定 1 kHz は BT コーデック確実通過帯）
silence_tone_amp     = 0.0001   # 振幅 0.0〜0.01（既定 0.0001 ≒ -80 dBFS）
```

動画読込時の初期ウィンドウサイズ上限は `[window].initial_screen_ratio` で変更できる（デフォルト 0.7 = モニタ作業領域の 70%）。

```toml
[window]
initial_screen_ratio = 0.7
```

動画読込時の初期再生速度は `[playback].speed` で指定できる（既定 1.00、上下キー操作で都度 0.05 単位の調整が可能）。

```toml
[playback]
speed = 1.00   # 1.00 = 等速、1.25 = 1.25 倍速
```

再生プレビューとシークバーホバー時のサムネイル抽出における HW デコード経路は `[playback]` セクションで制御できる。デフォルトは Windows 共通の `d3d11va` を最優先、フォールバックに NVIDIA NVDEC（`cuda`）を指定する。

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
- GUI フレームワーク：Qt 6.10 Widgets + Multimedia（LGPLv3）
- 映像コーデック：AV1（av1_nvenc, VBR CQ 28, preset p6）
- 音声コーデック：Opus 96kbps（libopus）
- ハードウェアアクセラレーション：NVIDIA NVENC（CUDA、動画変換時のみ使用）
- 外部ツール連携：ffmpeg / ffprobe（QProcess 経由）
