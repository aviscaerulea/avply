# avply — CLAUDE.md

## 開発環境

| 項目 | バージョン |
|------|-----------|
| コンパイラ | MSVC `v14.50`（VS 2026 Build Tools, x64） |
| CMake | `v3.25` 以上（`CMakeLists.txt` の要件） |
| Qt | `v6.10.3` MSVC2022 x64（インストール先は `CMakePresets.json` の `CMAKE_PREFIX_PATH` 参照） |
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

cmake が PATH 未追加の環境では `scoop install cmake` で追加する。

### webrtc-audio-processing の同梱

音声強調 DSP の WebRTC APM は `third_party/webrtc-audio-processing/`（`include/` + `lib/`）に事前ビルド済み静的ライブラリとして同梱する。VCS 追跡対象のため clone 後そのままビルドできる。バージョンは `v2.1`（freedesktop fork）。

更新・再生成する場合は meson + ninja で MSVC 静的ビルドする。

```powershell
# VS DevShell をロードした上で（build.ps1 と同じ Enter-VsDevShell 方式）
git clone --depth 1 https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
meson setup builddir --buildtype=release -Ddefault_library=static -Dcpp_std=c++20
meson compile -C builddir
```

成果物を以下の構成で配置する。meson は abseil を subproject として個別 lib へ分けて出力するため、lib をすべて集約する。

- `include/webrtc/`：`webrtc/` ツリーの全ヘッダ（`<webrtc/...>` 解決用）
- `include/absl/`：abseil の全ヘッダ（公開 API が `"absl/..."` を transitively include する）
- `lib/`：`builddir` 配下の全 `.a` を `.lib` へリネームして集約（`examples/` 配下は除外）

`CMakeLists.txt` は `WEBRTC_APM_ROOT` 配下の `include/` と `include/webrtc/` を include パスへ追加し、`lib/*.lib` を一括リンクする。`timeGetTime`（rtc_base の `SystemTimeNanos`）依存のため `winmm` もリンクする。

## テスト方法

### 実行

```powershell
# AVPLY_BUILD_TESTS=ON で再構成 → ビルド → ctest を一気通貫実行
pwsh.exe -File build-and-test.ps1

# クリーンビルドし直す場合
pwsh.exe -File build-and-test.ps1 -Reconfigure
```

`build-and-test.ps1` は `build.ps1` と独立した経路だ。`build.ps1` は本体 `avply.exe` のみ、`build-and-test.ps1` は `-DAVPLY_BUILD_TESTS=ON` でテストバイナリも含めて構築する。両者は同じ `out/` を共有する。`build-and-test.ps1` はキャッシュに `AVPLY_BUILD_TESTS=ON` が無ければ再構成するが、`build.ps1` は `CMakeCache.txt` の有無しか見ないため、ON が残ったキャッシュではテストも引き続きビルドする。本体のみへ戻すには `build.ps1 -Reconfigure` を使う。`AVPLY_BUILD_TESTS` は本体ターゲットに影響しないため、どちらの経路でも `avply.exe` は同一だ。

ctest は逐次実行する（`-j` 未指定）。その理由と、ヘッドレス実行向けの `QT_QPA_PLATFORM=offscreen` 指定は `build-and-test.ps1` のコメントが正だ。

### 対象

`tests/` 配下のテストバイナリ群。ユニットテスト対象は単体で完結して検証できるクラスに限定し、外部プロセス・音声デバイス・メディア再生の状態機械を含むクラス（例：`MainWindow` / `VideoView` / `AudioWorker` / `Encoder` / `SilenceTone` / `SingleInstance` / `SubtitleTranscriber`）は対象外とする。

| テスト | 対象 | 検証内容 |
|--------|------|----------|
| `test_OutputNamer` | `OutputNamer` | `_mod` シーケンス命名、衝突回避、Unicode パス、UUID フォールバック |
| `test_Settings` | `Settings` | デフォルト値、setter/getter ラウンドトリップ |
| `test_FfmpegRunner_path` | `Ffmpeg::ffprobePath` | 拡張子置換、空白を含むパス、相対パス絶対化 |
| `test_RangeSlider` | `RangeSlider` | ホイール・ホバー・ドラッグの各シグナル発火パターン |
| `test_SeekPreview` | `SeekPreview` | `showAt` のジオメトリ算出（中央配置・端クランプ・上下フリップ） |
| `test_SubtitleTrack` | `SubtitleTrack` | whisper-cli 出力行の解釈、再生位置からの検索、SRT の往復変換 |

### test_Settings 実装上の注意

`Settings` は `static` シングルトンで内部 `QSettings` を `NativeFormat / UserScope / "avply" / "avply"` で構築する。レジストリ位置は `HKCU\Software\avply\avply` 固定で、テスト側から差し替えられない（Native format は `QSettings::setPath` の対象外）。

当初 `RegOverridePredefKey` で `HKEY_CURRENT_USER` をテンポラリキーへリダイレクトする方式を試したが、`QSettings` の内部キャッシュとリダイレクトの相性で `QSettings` が setter の書き込みを反映しない事象が起きたため不採用とした。

最終的に実体 HKCU の退避→初期化→復元方式を採用する。`backupAndClear()` で `topmostWhilePlaying` / `singleInstance` / `aboveNormalPriority` の現値を退避してキーを削除し、テスト終了時に `restore()` で元の値を書き戻す。テスト完走時は開発者環境のレジストリに影響を残さない。

ただしテスト中に SIGSEGV 等で abort した場合は `restore()` が走らない。その場合は `regedit` で `HKCU\Software\avply\avply` 配下の 3 値を再設定する。avply を起動して右クリック設定メニューから操作しても良い。

## 実装上の注意点

### ビルド環境の注意

`windeployqt` は「VCINSTALLDIR is not set」警告を出す。この警告は無害で、DLL の配置は正常に完了する。

### 起動計測

環境変数 `AVPLY_STARTUP_TRACE` を空でも `0` でもない値にして起動すると、`StartupTrace` が実行ファイルと同階層の `avply_startup.log` へ里程標を追記する。プロセス生成〜main の経過は `StartupTrace::init` がヘッダ行として書く。以降の里程標の一覧は `StartupTrace::mark` の呼び出し箇所が正だ。代表例は QApplication 構築、可視化、LoadedMedia、初回映像フレーム、初回音声バッファ、ffprobe 完了だ。未設定時は `mark()` がアトミックフラグ 1 回の読み取りで抜けるため hot path に置いても実害はない。

`avply.log` を使わないのは、メッセージハンドラが Warning 以上しか記録しない設計のためだ。同じ label は最初の 1 回だけ記録するため、毎バッファ・毎フレームの経路から無条件に呼べる。

### 起動シーケンス

白フラッシュ抑制のためウィンドウは描画完了まで透明化し、復帰直後に `MainWindow::windowRevealed` を emit する。初回ファイルロード・ffmpeg パス検証・`SilenceTone` 起動はこのシグナルへ QueuedConnection で繋ぎ、可視化後に走らせる。復帰契機の使い分けと理由は `MainWindow` コンストラクタの `setWindowOpacity(0.0)` 周辺のコメントが正だ。

### 受け入れ可能ファイル

対応拡張子の一覧は README の「対応ファイル形式」節が正だ。
「動画か音声のみか」は `MainWindow::isAudioOnly()` で判定する。
対応音声拡張子は中身に関わらず音声扱いに倒す。mp3 等は ID3v2 APIC（アルバムアート）が `disposition.attached_pic` 付き video stream として ffprobe から返るため、ffprobe 結果だけで判定すると動画 UI に倒れてしまう。
動画拡張子は ffprobe 結果（`VideoInfo.codec` と `width`）で判定する。中身が音声のみのコンテナ（例：mkv 内が音声のみ）はコンパクト UI へ追従する。

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

### Encoder の生成タイミング

`Encoder` は変換・トリムの実行契機（`MainWindow::startOrCancel`）でのみ生成し、ffmpeg パスを渡す。
コンストラクタでは `m_encoder` を `nullptr` 初期値のまま保持する。

### ffmpeg パス設定

UI からの編集はせず、実行ファイルと同階層の `avply.toml`（ローカル上書きは `avply.local.toml`）の `[ffmpeg].path` で指定する。
未設定時のフォールバック順（scoop 既定パス → `PATH` 解決）は README の「設定」節が正だ。

### 再生設定

`avply.toml` の `[playback]` セクションで再生関連の挙動を制御する。各キーの意味・既定値は `avply.toml` のコメントが正だ。
再生速度はインスタンス生存中、ファイル切替後も現在値を保持する（`MainWindow::loadFile` が再適用する）。

`hw_decoder_priority` は QApplication 構築前に環境変数化する必要がある。そのため `Config::load()` は `main.cpp` の冒頭から呼ぶ。`Config::load()` 内部は exe ディレクトリ取得に `GetModuleFileNameW` を使い Qt 初期化に依存しないため、QApplication 未構築でも動作する。

### カーソルキーシーク設定

`avply.toml` の `[seek]` セクションで左右カーソルキー・Shift+左右カーソルキー・マウスホイールのスキップ量（ms）を指定する。各キーの既定値は `avply.toml` のコメントが正だ。
0 以下でそのキー・方向のシークを無効にする判定は `Config` ではクランプせず、`MainWindow::eventFilter` と `handleWheelInput` の呼び出し側ガードで行う。

### 音量設定

`avply.toml` の `[audio].volume` で再生音量の初期値を指定する（既定値・範囲は `avply.toml` のコメントが正だ）。
再生中の音量変更の入口は `MainWindow::changeVolume` で、操作は README の「キー・マウス操作」節が正だ。

`VideoView::setVolume` が `qBound(0.0, volume, 1.0)` で音量を 0.0〜1.0 へクランプしてから `AudioWorker` へ渡し、1.0 超の増幅は持たない。
過去に gain > 1.0 のブースト機能を実装したが、gain × playbackRate 高負荷時に resampler overshoot 起因のノイズを解消できず撤去した。
小音量の発言の底上げは音声強調（AGC2）が担う。

### 再生条件の一括リセット（G キー）

`G` キーの 2 段階リセット（中立値 → 起動時のデフォルト値）の挙動は README の「キー・マウス操作」節が正だ。音声強調は永続化しない仕様のため、1 回目・2 回目とも OFF になる。

起動時のデフォルト値は `MainWindow` コンストラクタで `m_initial*` メンバへスナップショットする（TOML 由来の速度・音量）。ユーザが後段でこれらを変更しても、スナップショット値を維持する。

リセット状態の追跡フラグ `m_gResetActive` は以下のように管理する。

- `toggleGReset` 内で `applyPlaybackState` 経由で直接 setter を更新する
  （`changePlaybackRate` 等の公開関数は経由しない）
- 公開関数 `changePlaybackRate` / `changeVolume` / `toggleSpeechEnhance` の末尾で
  `m_gResetActive = false` にクリアする
- これによりリセット状態中にユーザが任意の関連項目を手動操作すると自動でフラグが落ち、
  次の `G` 押下は再び「中立値リセット」として動作する

### 音声強調設定

再生時の WebRTC Audio Processing（APM）による会議音声のレベル均し機能。ノイズ抑制（NS）+ 自動ゲイン制御（AGC2）+ ハイパスフィルタ（HPF）を一括適用する。話者間の音量バラつきを自動で均し、マイク直結で小さく録れた発言を AGC2 の adaptive ゲインで持ち上げる。NS + HPF がこもり除去・低域カブリ抑制を兼ねる。

- 状態は ON/OFF の 2 値
- `C` キーの操作と起動時 OFF・非永続は README の「音声強調」節が正だ
- 状態表示：ステータスバーに `Clarity:ON/OFF` を常時表示する
- インスタンス生存中はファイル切替をまたいで状態を保つ（再生速度と同じ扱い）
- 旧 3 段階（レジストリ `speechEnhanceLevel`、0〜2）を ON/OFF へ簡素化した経緯  
  レジストリ永続化と toml の `[speech_enhance]` セクションを撤去した。
  残留する旧レジストリ値は掃除せず放置する（実害のない int 値 1 個のために削除コードを持たない）。

DSP チェーンは `SoundTouch（音程保持の時間圧縮）→ SpeechEnhancer（APM）→ 音量 → sink` の順だ。APM はテンポ・ピッチを変えないため、倍速再生時も SoundTouch が音程を保つ。
ただし等速（`|rate - 1.0| < 1e-6`）では `AudioWorker` が SoundTouch を経路ごと外し、decoder 出力を直接 `SpeechEnhancer` へ渡す。理由は `AudioWorker::onAudioBuffer` のバイパス判定のコメントが正だ。既定速度 1.00 はこのバイパス経路を通る。

#### モノラル処理

APM は AEC（エコーキャンセラ）非使用時に内部でモノラルへダウンミックスするため、`SpeechEnhancer` は明示的にモノラルで処理する。interleaved stereo をダウンミックス → APM 1ch 処理 → 同一サンプルを左右へ複製して interleaved へ戻す。OFF 時は APM を通さずステレオのまま素通しする。

#### 10ms フレーム蓄積

APM は 10ms 固定フレーム（48kHz で 480 サンプル）・deinterleaved float を要求する。`SpeechEnhancer` は可変長 `QAudioBuffer` を内部 FIFO に蓄積し、480 サンプル揃うごとに `ProcessStream` を呼ぶ。端数（<480）は次回へ持ち越す。出力は最大 10ms の処理遅延が乗るが、`AudioWorker` の `m_pendingTail` バックプレッシャと整合する。

#### スレッド前提

APM の `ApplyConfig` / `ProcessStream` / `Initialize` は同一スレッドから呼ぶ必要があるため、`SpeechEnhancer` の生成（`AudioWorker::start()`）と `setEnabled`（`setSpeechEnhanceEnabled`）はいずれも audio thread のスロット経由に限定する。根拠は `AudioWorker::start` のコメントが正だ。

#### プチノイズ（クリックノイズ）対策と内部定数

APM の最終リミッタがハードクリップして単発クリックを生む問題への対策として、入力プリアッテネーション（`kInputPreGain`）、`fixed_digital.gain_db = 0` 固定、`headroom_db` / `initial_gain_db` / `max_gain_change_db_per_second` の調整を恒久適用する。各値の根拠・実測結果・却下した値は `SpeechEnhancer.cpp` の定数定義と `buildConfig` のコメントが正だ。

NS レベル（`kNsLevel`）と AGC2 適応上限（`kMaxGainDb`）もコード固定で、toml では調整できない。

#### シーク・ファイル切替時のリセット

`SpeechEnhancer::reset()` でシーク・ファイル切替時に APM を `Initialize` し蓄積 / 出力 FIFO を破棄する。旧サンプルの遅延混入によるポップを防ぎ、ゲイン追従状態を持ち越さない。

### 字幕（whisper-cli）

`S` キーで ON/OFF する再生中の字幕生成。操作、表示位置、非永続、ファイル切替時の保持、音声のみ対象外は README の「字幕」節が正だ。whisper-cli の解決順とモデルの指定方法は README の「設定」節が正だ。ステータスバーに `Subtitle:ON/OFF/N/A` を常時表示する。N/A の条件は `MainWindow::updateSubtitleDisplay` が正だ。

方式は「先回り文字起こし」で、真のストリーミング認識ではない。外部プロセス whisper-cli がメディア全体を先頭から順に処理し、字幕は認識が追い越した区間から出る（利用者向けの説明は README の「字幕」節が正だ）。GPU 版の whisper-cli は実時間より十分速く進む想定だが、所要時間は未計測だ。CPU で追い付かない場合は無字幕が続くだけだ。自動で小さいモデルへ落とすフォールバックは持たず、`[subtitle].model` で手動指定する。

- 処理段階、キャッシュキー、パスの制約、失敗時の扱いは `SubtitleTranscriber.h` のクラスコメントが正だ
- SRT キャッシュの置き場と、`%TEMP%` に置かない理由は `MainWindow` コンストラクタのコメントが正だ
- 音声は再生経路から分岐せず、メディアから ffmpeg で直接抽出する
- そのため音声強調（APM）を通らない（強いデノイズは ASR の精度を上げない）
- `G` リセットは字幕の状態を維持する（生成に時間がかかり、誤って落とすと再開コストが高いため）
- 失敗してもラベルは ON のままにする
- 表示は `VideoOutput.qml` の `subtitleText` プロパティで映像上に重ねる
- QML 側で描く理由は同ファイルのコメントが正だ
- 同名上書き（`onEncoderReleaseFile`）と `loadFile` の冒頭で生成を止める
- 再開は `onProbeFinished` で ON のときだけ行う

### シーク時の旧バッファ破棄（シークゲート）・無音ランプ・再開時フェードイン

FFmpeg バックエンドはシークごとに `AudioRenderer` を破棄・再生成する。破棄は非同期のため、旧 renderer が直前に emit した数十 ms のバッファが `AudioWorker::reset` の後に届く。放置すると無音ランプの後に旧位置の断片が鳴り、新位置の音声との境目が不連続になる。sink を再起動していた旧方式では、空にした sink へ旧位置の断片を書き込むため「ザリッ」というノイズになっていた。

対策として `VideoView::setPosition` は `AudioWorker::reset(targetMs)` へシーク目標を渡す。`AudioWorker` はシークゲートを開き、開いている間は旧バッファの破棄判定を有効にする。Qt はシーク位置より前に終わるフレームを捨てるため、新 renderer のバッファは `QAudioBuffer::startTime()` がシーク目標近傍の媒体位置（µs）になる。そこで目標から `kSeekMatchToleranceUs`（1 秒）より離れたバッファを旧ストリームとして破棄し、近傍のバッファが届いたらゲートを閉じる。`startTime()` の意味は Qt 内部実装依存の非公開仕様のため、ゲートを開いてから `kSeekGateTimeoutMs`（500ms）経過後はフェイルセーフとして無条件に受理し、`avply.log` に警告を残す。シーク前後の位置差が 1 秒以下（`[seek]` を小さく設定した場合等）では判別できず従来動作になる。

`AudioWorker::reset` は sink を稼働させたまま、最後に sink へ書いたサンプル値から 0 へ `kRampMs`（5ms）で下る無音ランプを書き足す。以前の `QAudioSink::reset()` による WASAPI 再起動は、再生中の波形を任意点で切る段差が MPC-HC と同種の「パツッ」というクリックになっていた。Qt の renderer はバッファを表示時刻に送出し先読みしない。そのため sink に残る旧音声は通常プリロール（60ms、後述）+ 1 バッファ分（コーデックにより 20〜100ms）で、鳴り終わったあとランプで無音になる。sink が空なら旧音声は既に鳴り終わっているためランプを書かない。200ms の sink バッファはバースト吸収用で定常再生では埋まらないが、バースト直後のシークでは旧音声が最大 200ms 残り得る。これはシーク応答の遅れとして許容する。

アプリ終了（`AudioWorker::teardown`）でも sink を停止する前に、この 0 へ下る無音ランプを書き足す（sink が空なら書かない）。その後 sink バッファが空になるまで待ち、さらに `kDrainTailMarginMs`（20ms）の再生余裕を置いてから停止する。空になるまでの待ちの上限は sink バッファ長（200ms）+ `kRampMs`（5ms）+ `kDrainTailMarginMs`（20ms）で、GUI thread は終了時に最大約 250ms ブロックする。

あわせて `AudioWorker::reset` / `forceReset` / `recoverSink` の直後は、最初に受理したバッファの先頭 `kRampMs` へ線形フェードインを掛け、無音からの開始段差を丸める。`forceReset`（ファイル切替）は従来どおり sink を再起動する。

この 3 起点に加えて初回の sink 生成（`createAndStartSink`）でも、最初に受理したバッファの直前に `kPrerollMs`（60ms）の無音を書く（プリロール）。renderer が表示時刻ちょうどに送出するバッファは、sink がバッファ長ぶんを鳴らし終えた瞬間に届くため、到着のわずかな遅れで sink が空になり、持続音では無音の穴がクリックとして聞こえる（48kHz WAV 等速の実測で到着時残量 0〜8ms）。プリロールでストリーム全体を 60ms 後ろへずらし、残量を常に前倒しに保つ。起点の時点で無音を書くと最初のバッファが届く前に鳴り切って効果が消えるため、必ず最初のバッファ直前に書く。値の根拠は `kPrerollMs` のコメントが正だ。

### 高速再生時のサンプル欠落対策（SoundTouch WSOLA）

Qt 6.10 の `QAudioBufferOutput` は `setPitchCompensation(true)` を無視し、playback rate に関わらず生 decoded audio を decoder thread 速度で吐き出す。1.5x 再生では sink 消費レート（48000 frame/sec）に対して decoder 流入が 1.5 倍（72000 frame/sec）となり、`QAudioSink::write` が差分のサンプルの受領を拒否して捨てる。これが「プチプチ」というノイズの正体だった。

対策として `AudioWorker` 内で `SoundTouch`（WSOLA アルゴリズム）による時間圧縮を行う。`VideoView::setPlaybackRate` で `m_player->setPlaybackRate(rate)` と同時に `AudioWorker::setPlaybackRate(rate)` を呼び、`SoundTouch::setTempo(rate)` に反映する。`onAudioBuffer` では `putSamples` で投入し `receiveSamples` ループで取り出した後、「音声強調設定」節の DSP チェーン順で sink へ書き込む。出力流量が常に sink 消費レートと均衡するため、rate を上げても sink の受領拒否はほぼ起きない。SoundTouch がピッチも保つ。残る取りこぼしには `m_pendingTail` への退避と約 2 秒の overflow guard で備える。

`SoundTouch` は CMake FetchContent で `v2.4.0`（LGPL）を取り込む。`SOUNDTOUCH_DLL=OFF` / `INTEGER_SAMPLES=OFF`（float 入出力）/ `SOUNDSTRETCH=OFF`（CLI 不要）で構成する。

副次的な負荷軽減策も併用している。

- 作業バッファ再利用  
  `AudioWorker::onAudioBuffer` の `QByteArray` を毎呼び出し確保せず、
  メンバ変数 `m_workBuf` で再利用する（必要サイズに達するまで拡張のみ、ソース切替時に解放）。

- audio thread 優先度  
  `m_audioThread->start(QThread::HighPriority)` で GUI / decoder thread より高い優先度で
  起動する。`TimeCriticalPriority` は OS スケジューラ独占リスクがあるため避ける。

### サイレンストーン設定

`avply.toml` の `[audio]` セクションの `silence_tone_*` キーでサイレンストーン（BT アイドル復帰時のプチノイズ抑制用、常時不可聴トーン出力）を制御する。各キーの意味・既定値・クランプ範囲と根拠は `avply.toml` のコメントが正だ。
出力デバイス切替への追従は「出力デバイス切替への追従」節を参照する。

### 再生 sink の自己回復

画面録画ソフト等がシステム音声キャプチャ開始時にオーディオエンドポイントを再構成すると、avply の既存 WASAPI セッションが `AUDCLNT_E_DEVICE_INVALIDATED` で無効になり、再生音声だけが止まる（Aiseesoft Screen Recorder の録画開始で実機再現）。
放置すると sink の `bytesFree()` が恒久 0 となり、overflow guard の 2 秒毎破棄だけが続いて次のファイル切替（`forceReset`）まで無音が継続する。

対策として `AudioWorker::onAudioBuffer` 冒頭で sink の死活をチェックし、不健全なら sink を再生成して自動復帰する。
sink 再生成（`recoverSink`）の契機はこの自己回復と、「出力デバイス切替への追従」節の切替追従の 2 つだ。
検知条件は `AudioSinkHealth.h` の `isSinkUnhealthy` を `SilenceTone` と共用する（sink が null、`StoppedState`、`UnderrunError` 以外のエラーのいずれか）。`UnderrunError` は供給遅延で日常的に発生するため除外する。`AudioWorker` はこれに加えて `m_sinkDev` の喪失も不健全として扱う。再生成時は DSP 段の蓄積サンプル（旧セッション時代の遅延分）を破棄して現行デコード位置から鳴らし直す。自己回復側の再試行はデバイス完全消失中の空振り連発を避けるため 1 秒間隔に絞る（切替追従側にこの抑制は掛からない）。

### 出力デバイス切替への追従

`QAudioSink` は生成時点のデフォルト出力デバイスを掴んだままで、OS 側でデフォルトを切り替えても追従しない。
そのため `VideoView` が `QMediaDevices::audioOutputsChanged` を購読し、100ms の debounce を挟んで `AudioWorker::switchToDefaultDevice` を QueuedConnection で呼ぶ。
debounce は BT 接続シーケンス中の複数通知を 1 回へ集約する。

再生成の要否は `AudioWorker` 側で判定する。
`createAndStartSink` はデフォルト出力デバイスを明示指定して sink を生成し、その id を `m_sinkDeviceId` へ記録する。
`switchToDefaultDevice` はこの id と現デフォルトを比較し、異なる場合のみ `recoverSink` で作り直す。

- 判定を束縛側へ置くことで、デフォルト以外のデバイス増減による無用な再生成を抑える
- 自己回復が先に新デバイスへ束縛し直した直後の重複再生成も、同じ比較で抑える
- デフォルト出力デバイス不在（空 id）のときは現 sink を維持する
- 切替が捨てるのは sink 内と DSP 段の蓄積分のみで、再生位置は維持する（sink バッファは 200ms 相当）
- 追従するのは再生 sink だけで、サイレンストーン用 sink は `SilenceTone` が同じシグナルを購読して独立に追従する

### 出力ファイル名

命名規則（`_mod` 連番）と出力拡張子の対応は README の「出力ファイル」節が正だ。
`_mod` 名への上書き判定は `OutputNamer::isModName` が正だ。
上書き時は `Encoder` が既存ファイルを `.avply.bak` へ退避してから一時ファイルを置換し、失敗時は退避から復元する。
出力先が再生中のファイルと同一の場合、置換直前に `Encoder::releaseFileRequested` → `MainWindow::onEncoderReleaseFile` でプレイヤー・波形生成・サムネイル抽出・字幕生成のファイルハンドルを解放する。
ハンドル解放の完了が非同期の可能性があるため、退避リネームは 100ms 間隔で最大 1 秒リトライする。

### ウィンドウ表示位置

`MainWindow::loadFile()` は `centerOnMonitor` 引数でセンタリングの有無を切り替える。

- 新規プロセス起動時の初回ロード（「起動シーケンス」節）は `true` でモニタ作業領域の中央へ表示する
- D&D・「開く」ダイアログ・IPC 経由の再ロードはデフォルトの `false` で、現在のウィンドウ左上端 X,Y を維持してサイズのみ変更する

`centerOnMonitor=false` のとき、旧位置に対して新サイズが大きいとウィンドウがモニタ作業領域外へはみ出す。
これは「左上端 X,Y を維持する」というユーザ要求の必然的な副作用であり、はみ出した場合も位置を維持する（バグではない）。

## 参考

- README.md
