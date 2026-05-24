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
  VideoView.h/cpp     QMediaPlayer + QQuickView (VideoOutput) + AudioWorker による音声付きプレビュー
  VideoOutput.qml     プレビュー領域の QML（クリック・右クリック・ホイール受付）
  RangeSlider.h/cpp   開始〜終了区間を赤系でハイライト表示するスライダー
  Encoder.h/cpp       変換実行・進捗通知
  OutputNamer.h/cpp   出力ファイル名生成（{base}_clip.mp4 形式）
  Config.h/cpp        avply.toml 読み込み（ffmpeg_path 等）
  SilenceTone.h/cpp   BT アイドル復帰プチノイズ抑制用の常時不可聴トーン出力
  Normalizer.h/cpp    RMS コンプレッサ + メイクアップゲイン DSP（ノーマライズ）
  VoiceClarity.h/cpp  Biquad EQ 3 段カスケード DSP（音声明瞭化、こもり除去）
  AudioWorker.h/cpp   専用スレッドで Normalizer / VoiceClarity DSP と QAudioSink 書き込みを担うワーカ
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

### 再生設定

`avply.toml` の `[playback]` セクションで再生関連の挙動を制御する。

- `speed`: 動画読込時の初期再生速度（既定 1.00）。インスタンス起動中はファイル切替後も保持し、上下キーで 0.05 単位の調整が可能。
- `hw_decoder_priority`: QMediaPlayer の FFmpeg バックエンドに渡す HW デコーダ優先順位。`QT_FFMPEG_DECODING_HW_DEVICE_TYPES` と同形式（カンマ区切り）。デフォルト `"d3d11va,cuda"`。空文字で Qt 自動選択へフォールバックする。
- `thumbnail_hwaccel`: ThumbnailExtractor の ffmpeg `-hwaccel` 値。`"auto"` / `"d3d11va"` / `"cuda"` 等。`"none"` または空文字で `-hwaccel` 指定をスキップする。デフォルト `"auto"`。

`hw_decoder_priority` は QApplication 構築前に環境変数化する必要がある。そのため `Config::load()` は `main.cpp` の冒頭から呼ぶ。`Config::load()` 内部は exe ディレクトリ取得に `GetModuleFileNameW` を使い Qt 初期化に依存しないため、QApplication 未構築でも動作する。

### カーソルキーシーク設定

`avply.toml` の `[seek]` セクションで左右カーソルキーおよびマウスホイールのスキップ量（ms）を指定する。
デフォルトはいずれも 5000 ms。0 以下に設定するとそのキー／方向のシークが無効になる。

### 音量設定

`avply.toml` の `[audio].volume` で再生音量の初期値を指定する。
デフォルトは 1.00、範囲は 0.00〜1.00 にクランプする。
再生中はカーソルキー（上下）で ±0.05 ずつ調整できる（`MainWindow::changeVolume`）。

100% 超のソフトウェアブーストはサポートしない。
過去に gain > 1.0 のブースト機能を実装したが、gain × playbackRate 高負荷時に resampler overshoot 起因のノイズを解消できず撤去した。
現在の `AudioWorker`（`QAudioBufferOutput` + 専用スレッド + `QAudioSink`）は γ RMS コンプレッサを搭載し、出力が構造的に ±0.97 を超えないため同問題を回避している。
ユーザが音量底上げを必要とする場合は、ノーマライズ（N キー）を使用すること。

### 再生条件の一括リセット（G キー）

`G` キーで再生速度・音量・Normalize レベル・Clarity レベルの 4 項目をトグルする。
1 回目で「中立値」（速度 1.00、音量 1.00、Normalize 0、Clarity 0）へ、2 回目で「起動時のデフォルト値」へ復元する。

起動時のデフォルト値は `MainWindow` コンストラクタで `m_initial*` メンバへスナップショットする（TOML 由来の速度・音量、レジストリ由来の Normalize・Clarity）。ユーザが後段でこれらを変更しても、スナップショット値は維持される。

リセット状態の追跡フラグ `m_gResetActive` は以下のように管理する。

- `toggleGReset` 内で `applyPlaybackState` 経由で直接 setter / Settings を更新する（`changePlaybackRate` 等の公開関数は経由しない）
- 公開関数 `changePlaybackRate` / `changeVolume` / `cycleNormalize` / `cycleVoiceClarity` の末尾で `m_gResetActive = false` にクリアする
- これによりリセット状態中にユーザが任意の関連項目を手動操作すると自動でフラグが落ち、次の `G` 押下は再び「中立値リセット」として動作する

### ノーマライズ設定

再生時のアップワード RMS コンプレッサによる小音量持ち上げ機能。閾値未満の小音量のみをブーストし、閾値以上の大音量はゲイン 1.0 で素通しする。大音量を一切押し上げないため出力は元のピークを超えず、後段リミッタが恒常的に発火しないため音割れ・歪みを生まない。
シーク・ファイル切替直後の `kRmsWindowMs`（10ms）は RMS 追跡器の収束を待つため `currentGain=1.0` で素通りし、その後にブーストを開始する（warmup）。シーク直後のポップノイズ抑制が目的だ。

- 強度は 4 段階：Off / 小 / 中 / 大（デフォルト：中。`Settings::normalizeLevel` の既定値 `2`）
- 切替操作：`N` キーのみ。押下する度に Off → 小 → 中 → 大 → Off の順で循環する
- 状態表示：ステータスバーに「Normalize:0〜3」を常時表示する（0=Off / 1=小 / 2=中 / 3=大）
- 永続化：レジストリ（`HKEY_CURRENT_USER\Software\avply\avply\normalizeLevel`、int 値 0〜3）

Off ↔ ON 切替時は 50ms のゲインランプで段差を回避する。なお Off → ON 直後は RMS 追跡器の warmup（10ms、`kRmsWindowMs`）が直列に乗るため、体感ランプ時間は最大で 60ms 程度になる。
ON 状態間の強度変更（小 ↔ 中 ↔ 大）は threshold / makeup の差し替えのみで、コンプレッサ追従の特性が滑らかに変化するためランプは発生させない。

DSP パラメータ（強度別の threshold / makeup は `avply.toml` の `[normalizer]` セクションで指定する。それ以外は `Normalizer.cpp` の constexpr 定数）：

| パラメータ | 共通 | 小 | 中 | 大 |
|-----------|------|----|----|----|
| Threshold（持ち上げ境界） | - | -20.0 dB | -25.0 dB | -30.0 dB |
| Max boost（最大ブースト量） | - | +5.0 dB | +10.0 dB | +13.0 dB |
| Ratio（閾値未満の持ち上げ率） | 6:1 | - | - | - |
| Attack | 20 ms | - | - | - |
| Release | 250 ms | - | - | - |
| Limiter knee | 0.90 | - | - | - |
| Limiter ceiling | 0.97 | - | - | - |
| RMS window | 10 ms | - | - | - |
| ON/OFF ramp | 50 ms | - | - | - |

avply.toml 設定例（既定値）：

```toml
[normalizer]
threshold_db_small  = -20.0
threshold_db_medium = -25.0
threshold_db_large  = -30.0
makeup_db_small  =   5.0
makeup_db_medium =  10.0
makeup_db_large  =  13.0
```

threshold は -60.0〜0.0 dBFS、makeup（最大ブースト量）は 0.0〜24.0 dB にクランプする。
アップワード設計により大音量は素通しで出力が元のピークを超えないため、softLimit（knee 0.90 ≒ -0.9dBFS、ceiling 0.97）は通常時不発で、フルスケール直前のピークと resampler overshoot（最大 ~2%）のみを穏やかに頭打ちする。`0.97 × 1.02 ≒ 0.99` でハードクリップに触れない安全設計だ。

### 音声明瞭化設定

再生時の Biquad EQ による人声の聞き取りやすさ改善機能。こもり気味の音声の子音帯域を持ち上げ、低域カブリを抑える。
DSP チェーン上は SoundTouch 出力直後・Normalizer 前段に挿入する。EQ のピーキングブーストで振幅が拡張されても、後段 Normalizer のリミッタ（±0.97）が自然に上限を保証する設計だ。

- 強度は 4 段階：Off / 小 / 中 / 大（デフォルト：中。`Settings::voiceClarityLevel` の既定値 `2`）
- 切替操作：`V` キーのみ。押下する度に Off → 小 → 中 → 大 → Off の順で循環する
- 状態表示：ステータスバーに「Clarity:0〜3」を常時表示する（0=Off / 1=小 / 2=中 / 3=大）
- 永続化：レジストリ（`HKEY_CURRENT_USER\Software\avply\avply\voiceClarityLevel`、int 値 0〜3）

Off ↔ ON 切替時は 50ms の線形ランプで raw 信号と processed 信号をクロスフェードしてクリックノイズを避ける。
ON 状態間の強度変更（小 ↔ 中 ↔ 大）は Biquad 係数のみ差し替え、フィルタ内部状態（x1, x2, y1, y2）は維持する。ゲイン差が ±2dB と小さいため、状態継続による過渡応答も聴感上のポップにはならない。

DSP パラメータ（強度別の peak / shelf ゲインは `avply.toml` の `[voice_clarity]` セクションで指定する。
フィルタ周波数 / Q は `VoiceClarity.cpp` の constexpr 定数。RBJ Audio EQ Cookbook 準拠の Biquad 係数算出）：

| 段 | フィルタ種別 | 共通パラメータ | 小 | 中 | 大 |
|----|-------------|---------------|----|----|----|
| 1 | High-pass | fc=100Hz, Q=0.707（Butterworth） | gain なし | gain なし | gain なし |
| 2 | Peaking EQ | fc=3000Hz, Q=1.0 | +3dB | +5dB | +7dB |
| 3 | High-shelf | fc=8000Hz, Q=0.707 | +1dB | +2dB | +3dB |

avply.toml 設定例（既定値）：

```toml
[voice_clarity]
peak_db_small  = 3.0
peak_db_medium = 5.0
peak_db_large  = 7.0
shelf_db_small  = 1.0
shelf_db_medium = 2.0
shelf_db_large  = 3.0
```

peak / shelf いずれも 0.0〜12.0 dB にクランプする。負値はカット方向で明瞭化の趣旨に反するため許容しない。

各 Biquad は Direct Form I 実装で、チャンネル × 段ごとに独立した状態（x1, x2, y1, y2）を保持する。
`reset()` でシーク・ファイル切替時に状態をゼロクリアし、旧サンプルの遅延が混入してインパルス的ポップが出るのを防ぐ。

**高速再生時のサンプル欠落対策（SoundTouch WSOLA）**

Qt 6.10 の `QAudioBufferOutput` は `setPitchCompensation(true)` を無視し、playback rate に関わらず生 decoded audio を decoder thread 速度で吐き出す。1.5x 再生では sink 消費レート（48000 frame/sec）に対して decoder 流入が 1.5 倍（72000 frame/sec）となり、差分のサンプルが `QAudioSink::write` で受領拒否され捨てられる。これが「プチプチ」というノイズの正体だった。

対策として `AudioWorker` 内で `SoundTouch`（WSOLA アルゴリズム）による時間圧縮を行う。`VideoView::setPlaybackRate` で `m_player->setPlaybackRate(rate)` と同時に `AudioWorker::setPlaybackRate(rate)` を呼び、`SoundTouch::setTempo(rate)` に反映する。`onAudioBuffer` では `putSamples` で投入 → `receiveSamples` ループで取り出し → Normalizer / 音量 / sink へ書き込む。出力流量が常に sink 消費レートと均衡するため、rate に関わらず受領拒否（underrun）はゼロになる。ピッチも保持される。

`SoundTouch` は CMake FetchContent で 2.4.0（LGPL）を取り込んでいる。`SOUNDTOUCH_DLL=OFF` / `INTEGER_SAMPLES=OFF`（float 入出力）/ `SOUNDSTRETCH=OFF`（CLI 不要）で構成する。

副次的な負荷軽減策も併用している。

- **DSP 間引き**：`sqrt / log10 / pow` を含む重計算（ターゲットゲイン算出）を RMS 窓長（10ms）相当のフレーム数に 1 回だけ実行する。アタック/リリース IIR は毎フレーム動かして追従性を維持する。10ms の階段更新は Attack 20ms / Release 250ms に対して十分細かく、聴感上の差異はない
- **作業バッファ再利用**：`AudioWorker::onAudioBuffer` の `QByteArray` を毎呼び出し確保せずメンバ変数 `m_workBuf` で再利用する（必要サイズに達するまで拡張のみ、ソース切替時に解放）
- **audio thread 優先度**：`m_audioThread->start(QThread::HighPriority)` で GUI / decoder thread より高い優先度で起動する。`TimeCriticalPriority` は OS スケジューラ独占リスクがあるため避ける

### サイレンストーン設定

`avply.toml` の `[audio]` セクションでサイレンストーン（BT アイドル復帰時のプチノイズ抑制用、常時不可聴トーン出力）を制御する。

- `silence_tone_enabled`: ON/OFF（既定 `true`）。`false` でトーン出力を停止し、OS への常時音声出力を完全に止める。スピーカー環境や有線 DAC 環境ではユーザ判断で OFF にできる
- `silence_tone_freq_hz`: トーン周波数（既定 1000.0、20〜20000 Hz にクランプ）。1 kHz は SBC 等 BT コーデックの確実なパスバンド内で、コーデックを「アクティブ」状態に保つ
- `silence_tone_amp`: 振幅（0.0〜1.0、1.0=16bit フルスケール、既定 0.0001=約 -80 dBFS）。設定ミスによる過大音量を避けるため上限 0.01 にクランプする

`SilenceTone` は `QMediaDevices::audioOutputsChanged` を購読してデフォルト出力デバイス変化（BT 接続/切断、USB DAC 抜き挿し）に追従し、自動で sink を再生成する。

### 出力ファイル名

入力ファイルと同一フォルダに `<元ファイルベース名>_clip.<拡張子>` で出力する。
同名が既に存在する場合は `_clip_2.<拡張子>`、`_clip_3.<拡張子>` の順で衝突回避する。

拡張子はモードと入力種別で決定する。

| モード | 入力 | 出力拡張子 |
|--------|------|-----------|
| 変換 | 動画 | `.mp4`（AV1 + Opus） |
| 変換 | 音声のみ | `.opus`（libopus） |
| トリム | 動画・音声どちらも | 入力と同じ拡張子 |

### ウィンドウ表示位置

`MainWindow::loadFile()` は `centerOnMonitor` 引数でセンタリングの有無を切り替える。

- 新規プロセス起動時の初回ロード（コンストラクタ内の `QTimer::singleShot` 経由）は `true` を渡してモニタ作業領域の中央へ表示する
- D&D・「開く」ダイアログ・IPC 経由の再ロードはデフォルトの `false` で、現在のウィンドウ左上端 X,Y を維持してサイズのみ変更する

`centerOnMonitor=false` のとき、旧位置に対して新サイズが大きいとウィンドウがモニタ作業領域外へはみ出すケースがある。
これは「左上端 X,Y を維持する」というユーザ要求の必然的な副作用であり、バグではない。
画面外補正（`move()` による自動再配置）はあえて行わない。

## 参考

- @README.md
