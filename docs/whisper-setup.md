---
title: 字幕のための whisper-cli 導入手順
---

[![日本語](https://img.shields.io/badge/lang-日本語-red)](whisper-setup.md)
[![English](https://img.shields.io/badge/lang-English-blue)](en/whisper-setup.md)

# 字幕のための whisper-cli 導入手順

avply の字幕は、音声認識ツール whisper-cli（whisper.cpp）を外部プログラムとして呼び出して作ります。
whisper-cli は avply に同梱していないため、別途インストールが必要です。
このページでは、whisper-cli とモデルファイルを用意して avply から使えるようにするまでを順に説明します。

## 用意するもの

| 必要なもの | 説明 |
| --- | --- |
| whisper-cli | whisper.cpp の実行ファイル。GPU 版と CPU 版がある |
| モデルファイル | 音声認識の学習済みデータ（`ggml-*.bin`）。数百 MB〜1.6GB |
| NVIDIA ドライバ | GPU 版を使うときだけ必要。CUDA Toolkit のインストールは不要 |

## 1. GPU の有無の確認

NVIDIA の GPU があるなら GPU 版を使います。認識が実時間より速く進み、字幕がすぐに追い付きます。
PowerShell を開きます。次に以下のコマンドを実行します。

```powershell
nvidia-smi
```

GPU の名前とドライバのバージョンを含む表が出れば GPU 版を使えます。
「認識されていません」のようなエラーが出るときは、CPU 版へ進んでください。

## 2. whisper-cli の導入

### GPU 版（推奨）

次の URL から ZIP ファイルをダウンロードします。

```
https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.2/whisper-cublas-12.4.0-bin-x64.zip
```

ダウンロードした ZIP を `C:\tools\whisper\` へ展開します。
展開後に `C:\tools\whisper\Release\whisper-cli.exe` があることを確認してください。
展開先は英数字だけのパスにします。whisper-cli は日本語などを含むパスを開けないためです。

### CPU 版

Scoop が使える環境では、次のコマンドで入ります。

```powershell
scoop install whisper-cpp
```

Scoop で入れた whisper-cli は avply が自動で見つけるため、パスの設定は不要です。
CPU 版は認識が遅いため、後述のモデルは小さいものを選んでください。

## 3. モデルファイルの入手

モデルファイルは、以下から 1 つ選びます。

| ファイル名 | サイズ | 向いている環境 |
| --- | --- | --- |
| `ggml-large-v3-turbo.bin` | 約 1.6GB | GPU 版。日本語の実用品質にはこれを推奨 |
| `ggml-medium.bin` | 約 1.5GB | CPU 版で品質を優先するとき |
| `ggml-small.bin` | 約 490MB | CPU 版で速さを優先するとき |

`C:\tools\whisper\models\` フォルダを作ります。次に PowerShell で以下を実行してダウンロードします。
ファイル名の部分は選んだものに置き換えてください。

```powershell
mkdir C:\tools\whisper\models
curl.exe -L -o C:\tools\whisper\models\ggml-large-v3-turbo.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin
```

モデルファイルの置き場も英数字だけのパスにします。

## 4. avply へのパス設定

`avply.exe` と同じフォルダに `avply.local.toml` というファイルを作ります。
次にメモ帳などで開きます。最後に以下を書いて保存します。パスは自分の環境に合わせてください。

```toml
[subtitle]
whisper_path = "C:/tools/whisper/Release/whisper-cli.exe"
model = "C:/tools/whisper/models/ggml-large-v3-turbo.bin"
```

パスの区切りは `/` で書けます。
CPU 版を Scoop で入れた場合は、`whisper_path` の行を省略できます。

## 5. 動作の確認

avply を起動し直します。設定は起動時にだけ読み込むためです。
次に動画ファイルを開きます。
最後に `S` キーを押します。

画面下部の表示が `Subtitle:0%` になり、数字が増えていきます。
`Subtitle:ON` に変わると、映像の下部に字幕が出ます。
同じ動画を次に開くときは、キャッシュからすぐに字幕が出ます。

## うまくいかないとき

| 症状 | 原因 | 対処 |
| --- | --- | --- |
| `S` を押しても `Subtitle:N/A` のまま | whisper-cli かモデルファイルのパスが違う | `avply.local.toml` のパスを確認し、avply を起動し直す |
| `Subtitle:ERR` になる | whisper-cli が異常終了した | `avply.exe` と同じフォルダの `avply.log` の末尾を見る |
| 数字が増えず、字幕が出ない | CPU 版で大きなモデルを使っている | `ggml-small.bin` に変える |
| DLL が見つからないというエラーが `avply.log` に出る | NVIDIA ドライバが古い | ドライバを更新する |
