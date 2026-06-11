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

### webrtc-audio-processing の同梱

音声強調 DSP の WebRTC APM は `third_party/webrtc-audio-processing/`（`include/` + `lib/`）に事前ビルド済み静的ライブラリとして同梱する。VCS 追跡対象のため clone 後そのままビルドできる。バージョンは v2.1（freedesktop fork）。

更新・再生成する場合は meson + ninja で MSVC 静的ビルドする。

```powershell
# VS DevShell をロードした上で（build.ps1 と同じ Enter-VsDevShell 方式）
git clone --depth 1 https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
meson setup builddir --buildtype=release -Ddefault_library=static -Dcpp_std=c++20
meson compile -C builddir
```

成果物を以下の構成で配置する。abseil は subproject として個別 lib へ分かれて出力されるため lib をすべて集約する。

- `include/webrtc/`：`webrtc/` ツリーの全ヘッダ（`<webrtc/...>` 解決用）
- `include/absl/`：abseil の全ヘッダ（公開 API が `"absl/..."` を transitively include する）
- `lib/`：`builddir` 配下の全 `.a` を `.lib` へリネームして集約（`examples/` 配下は除外）

`CMakeLists.txt` は `WEBRTC_APM_ROOT` 配下の `include/` と `include/webrtc/` を include パスへ追加し、`lib/*.lib` を一括リンクする。`timeGetTime`（rtc_base の `SystemTimeNanos`）依存のため `winmm` もリンクする。

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
  OutputNamer.h/cpp   出力ファイル名生成（{base}_mod.mp4 形式）
  Config.h/cpp        avply.toml 読み込み（ffmpeg_path 等）
  SilenceTone.h/cpp   BT アイドル復帰プチノイズ抑制用の常時不可聴トーン出力
  SpeechEnhancer.h/cpp WebRTC APM ラッパ（NS + AGC2 + HPF による音声強調 DSP）
  AudioWorker.h/cpp   専用スレッドで SpeechEnhancer DSP と QAudioSink 書き込みを担うワーカ
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

- `speed`: 動画読込時の初期再生速度（既定 1.00）。インスタンス起動中はファイル切替後も保持し、`.` / `,` キーで 0.05 単位の調整が可能。
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
現在の `AudioWorker`（`QAudioBufferOutput` + 専用スレッド + `QAudioSink`）は出力前に音量を ±1.0 へクランプするため同問題を回避している。
ユーザが小音量の発言を底上げしたい場合は、音声強調（N キー）の AGC2 を使用すること。

### 再生条件の一括リセット（G キー）

`G` キーで再生速度・音量・音声強調レベルの 3 項目をトグルする。
1 回目で「中立値」（速度 1.00、音量 1.00、音声強調 0）へ、2 回目で「起動時のデフォルト値」へ復元する。

起動時のデフォルト値は `MainWindow` コンストラクタで `m_initial*` メンバへスナップショットする（TOML 由来の速度・音量、レジストリ由来の音声強調レベル）。ユーザが後段でこれらを変更しても、スナップショット値は維持される。

リセット状態の追跡フラグ `m_gResetActive` は以下のように管理する。

- `toggleGReset` 内で `applyPlaybackState` 経由で直接 setter / Settings を更新する（`changePlaybackRate` 等の公開関数は経由しない）
- 公開関数 `changePlaybackRate` / `changeVolume` / `cycleSpeechEnhance` の末尾で `m_gResetActive = false` にクリアする
- これによりリセット状態中にユーザが任意の関連項目を手動操作すると自動でフラグが落ち、次の `G` 押下は再び「中立値リセット」として動作する

### 音声強調設定

再生時の WebRTC Audio Processing（APM）による会議音声のレベル均し機能。ノイズ抑制（NS）+ 自動ゲイン制御（AGC2）+ ハイパスフィルタ（HPF）を一括適用する。話者間の音量バラつきを自動で均し、マイク直結で小さく録れた発言を AGC2 の adaptive ゲインで持ち上げる。NS + HPF がこもり除去・低域カブリ抑制を兼ねる。

- 強度は 3 段階：Off / 標準 / 強（デフォルト：標準。`Settings::speechEnhanceLevel` の既定値 `1`）
- 切替操作：`N` キーのみ。押下する度に Off → 標準 → 強 → Off の順で循環する
- 状態表示：ステータスバーに「Clarity:0〜2」を常時表示する（0=Off / 1=標準 / 2=強）
- 永続化：レジストリ（`HKEY_CURRENT_USER\Software\avply\avply\speechEnhanceLevel`、int 値 0〜2）

DSP チェーンは `SoundTouch（音程保持の時間圧縮）→ SpeechEnhancer（APM）→ 音量 → sink` の順だ。APM はテンポ・ピッチを変えないため、倍速再生時も音程は SoundTouch により保たれる。

**モノラル処理**

APM は AEC（エコーキャンセラ）非使用時に内部でモノラルへダウンミックスするため、`SpeechEnhancer` は明示的にモノラルで処理する。interleaved stereo をダウンミックス → APM 1ch 処理 → 同一サンプルを左右へ複製して interleaved へ戻す。Off 時は APM を通さずステレオのまま素通しする。

**10ms フレーム蓄積**

APM は 10ms 固定フレーム（48kHz で 480 サンプル）・deinterleaved float を要求する。`SpeechEnhancer` は可変長 `QAudioBuffer` を内部 FIFO に蓄積し、480 サンプル揃うごとに `ProcessStream` を呼ぶ。端数（<480）は次回へ持ち越す。出力は最大 10ms の処理遅延が乗るが、`AudioWorker` の `m_pendingTail` バックプレッシャと整合する。

**スレッド前提**

APM の `ApplyConfig` / `ProcessStream` / `Initialize` は同一スレッドから呼ぶ必要がある。そのため `SpeechEnhancer` の生成は `AudioWorker::start()` スロット（audio thread）で行い、affinity を確定する。`setLevel` も `setSpeechEnhanceLevel` スロット経由で audio thread からのみ呼ぶ。

**プチノイズ（クリックノイズ）対策**

APM の最終リミッタはピークを 1.0 へ頭打ちにする。WebRTC AGC2 は VoIP のマイクレベル入力（full-scale から余裕のある音量）を前提とするため、既にほぼ full-scale で録れた会議音声をそのまま入れると adaptive ゲインが過剰ブーストし、リミッタがハードクリップして単発クリックを生む。対策として以下を恒久適用する。

- 入力プリアッテネーション：APM 投入前にモノラルサンプルを約 -6dB（`SpeechEnhancer.cpp` の `kInputPreGain = 0.5f`）減衰させ、AGC2 が期待する余裕を作る。これで小音量発言の持ち上げを保ったまま全強度でクリップを根絶する。実測で -6dB なら Off / 標準 / 強すべてでクリップフレーム 0
- `fixed_digital.gain_db = 0` 固定：adaptive の後・リミッタの前に効く固定ブーストは決定的なクリップ源であり、プリアッテネーション併用でも +3/+6dB で再クリップしたため恒久無効化した。強度差は NS レベルのみで決まる
- `headroom_db = 4` / `initial_gain_db = 6`：headroom は full-scale から差し引いた値が AGC2 の出力ターゲットになる。小さいほどターゲットが上がり小声を強く持ち上げる。大声は既にターゲット以上のため影響を受けず、クリップ耐性もプリアッテネーションで担保されるため変わらない。4dB で小声を十分持ち上げつつ全強度でクリップフレーム 0 を維持する（実測）。`initial_gain_db` は既定 15dB から控えめにして再生直後の過大ブーストを抑える
- `max_gain_change_db_per_second = 300`：適応ゲインが目標へ収束する速度の上限。既定 6dB/s では小声を +24dB 持ち上げるのに約 4 秒かかり、発話冒頭がゲイン追従に間に合わず聞こえない。レートリミッタを大きく開放して各発話冒頭の追従を可能な限りタイトにする。速めても offline 計測でクリップフレーム 0・maxDisc 不変のためクリックは再発しない（実測）。なお 100 dB/s 以上は全体平均が頭打ちで、実質の律速は AGC 内部の小声検知レイテンシ（Config 非公開）。`initial_gain_db` を上げれば初期位置から速く立ち上がるが、再生開始直後の大音量がリミッタを叩きクリップが再発するため 6 に据え置く

強度差は NS（ノイズ抑制）レベルのみで付く。強度別の NS レベルは `avply.toml` の `[speech_enhance]` セクションで指定する。

| パラメータ | 標準 | 強 |
|-----------|------|----|
| NS レベル（0=Low / 1=Moderate / 2=High / 3=VeryHigh） | 1 | 2 |

avply.toml 設定例（既定値）：

```toml
[speech_enhance]
ns_level_standard = 1
ns_level_strong   = 2
```

NS レベルは 0〜3 にクランプする。`3`（VeryHigh）は声まで削るため非推奨。

AGC2 適応上限（`adaptive_digital.max_gain_db`）は強度別に分けず `SpeechEnhancer.cpp` の `kMaxGainDb = 40.0f` でコード固定する。offline 計測で会議音声の小声は headroom で決まる出力ターゲットまでの持ち上げで足り、適応ゲインが天井に届かず 30/40/50dB のいずれでも出力レベル・クリップ指標が完全に一致したため、強度別に分けても無意味と判明した。入力プリアッテネーションと並び、クリップ耐性・有効性に直結する内部定数のため toml では調整できない。
`reset()` でシーク・ファイル切替時に APM を `Initialize` し蓄積 / 出力 FIFO を破棄する。旧サンプルの遅延混入によるポップを防ぎ、ゲイン追従状態を持ち越さない。

**高速再生時のサンプル欠落対策（SoundTouch WSOLA）**

Qt 6.10 の `QAudioBufferOutput` は `setPitchCompensation(true)` を無視し、playback rate に関わらず生 decoded audio を decoder thread 速度で吐き出す。1.5x 再生では sink 消費レート（48000 frame/sec）に対して decoder 流入が 1.5 倍（72000 frame/sec）となり、差分のサンプルが `QAudioSink::write` で受領拒否され捨てられる。これが「プチプチ」というノイズの正体だった。

対策として `AudioWorker` 内で `SoundTouch`（WSOLA アルゴリズム）による時間圧縮を行う。`VideoView::setPlaybackRate` で `m_player->setPlaybackRate(rate)` と同時に `AudioWorker::setPlaybackRate(rate)` を呼び、`SoundTouch::setTempo(rate)` に反映する。`onAudioBuffer` では `putSamples` で投入 → `receiveSamples` ループで取り出し → `SpeechEnhancer`（APM）→ 音量 → sink へ書き込む。出力流量が常に sink 消費レートと均衡するため、rate に関わらず受領拒否（underrun）はゼロになる。ピッチも保持される。

`SoundTouch` は CMake FetchContent で 2.4.0（LGPL）を取り込んでいる。`SOUNDTOUCH_DLL=OFF` / `INTEGER_SAMPLES=OFF`（float 入出力）/ `SOUNDSTRETCH=OFF`（CLI 不要）で構成する。

副次的な負荷軽減策も併用している。

- **作業バッファ再利用**：`AudioWorker::onAudioBuffer` の `QByteArray` を毎呼び出し確保せずメンバ変数 `m_workBuf` で再利用する（必要サイズに達するまで拡張のみ、ソース切替時に解放）
- **audio thread 優先度**：`m_audioThread->start(QThread::HighPriority)` で GUI / decoder thread より高い優先度で起動する。`TimeCriticalPriority` は OS スケジューラ独占リスクがあるため避ける

### サイレンストーン設定

`avply.toml` の `[audio]` セクションでサイレンストーン（BT アイドル復帰時のプチノイズ抑制用、常時不可聴トーン出力）を制御する。

- `silence_tone_enabled`: ON/OFF（既定 `true`）。`false` でトーン出力を停止し、OS への常時音声出力を完全に止める。スピーカー環境や有線 DAC 環境ではユーザ判断で OFF にできる
- `silence_tone_freq_hz`: トーン周波数（既定 1000.0、20〜20000 Hz にクランプ）。1 kHz は SBC 等 BT コーデックの確実なパスバンド内で、コーデックを「アクティブ」状態に保つ
- `silence_tone_amp`: 振幅（0.0〜1.0、1.0=16bit フルスケール、既定 0.0001=約 -80 dBFS）。設定ミスによる過大音量を避けるため上限 0.01 にクランプする

`SilenceTone` は `QMediaDevices::audioOutputsChanged` を購読してデフォルト出力デバイス変化（BT 接続/切断、USB DAC 抜き挿し）に追従し、自動で sink を再生成する。

### 再生 sink の自己回復

画面録画ソフト等がシステム音声キャプチャ開始時にオーディオエンドポイントを再構成すると、avply の既存 WASAPI セッションが `AUDCLNT_E_DEVICE_INVALIDATED` で無効化され、再生音声だけが止まる。（Aiseesoft Screen Recorder の録画開始で実機再現）
放置すると sink の `bytesFree()` が恒久 0 となり、overflow guard の 2 秒毎破棄だけが続いて次のシークまで無音が継続する。

対策として `AudioWorker::onAudioBuffer` 冒頭で sink の死活をチェックし、不健全なら sink を再生成して自動復帰する。検知条件は `SilenceTone::healthCheck` と同型だ（`StoppedState`、または `UnderrunError` 以外のエラー。`UnderrunError` は供給遅延で日常的に発生するため除外）。再生成時は DSP 段の蓄積サンプル（旧セッション時代の遅延分）を破棄して現行デコード位置から鳴らし直す。再試行はデバイス完全消失中の空振り連発を避けるため 1 秒間隔に絞る。

### 出力ファイル名

入力ファイルと同一フォルダに `<元ファイルベース名>_mod.<拡張子>` で出力する。
同名が既に存在する場合は `_mod2.<拡張子>`、`_mod3.<拡張子>` の順で衝突回避する。
ベース名末尾が既に `_mod` または `_mod<数字>` の場合は同名へ上書き出力する（`OutputNamer::isModName`）。
上書き時は `Encoder` が既存ファイルを `.avply.bak` へ退避してから一時ファイルを置換し、失敗時は退避から復元する。
出力先が再生中のファイルと同一の場合、置換直前に `Encoder::releaseFileRequested` → `MainWindow::onEncoderReleaseFile` でプレイヤー・波形生成・サムネイル抽出のファイルハンドルを解放する。
ハンドル解放の完了が非同期の可能性があるため、退避リネームは 100ms 間隔で最大 1 秒リトライする。

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
