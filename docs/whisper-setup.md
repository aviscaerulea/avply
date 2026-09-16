---
title: 字幕のモデルと GPU
---

[![日本語](https://img.shields.io/badge/lang-日本語-red)](whisper-setup.md)
[![English](https://img.shields.io/badge/lang-English-blue)](en/whisper-setup.md)

# 字幕のモデルと GPU

avply の字幕は、音声認識エンジンを内蔵しています。別のソフトを入れる必要はありません。
必要なのは音声認識モデルだけで、これは avply が自動でダウンロードします。
このページでは、その流れと、モデルの選び方、GPU をどう使うかを説明します。

## 初回の自動ダウンロード

動画を開いて `S` キーを押すと、モデルのダウンロードを確認するダイアログが出ます。
「はい」を選ぶとダウンロードが始まり、画面下部に `Subtitle:DL 42%` のように進み具合を表示します。
ダウンロードが終わると認識が始まり、`Subtitle:0%` から数字が増えていきます。
`Subtitle:ON` に変わると、映像の下部に字幕が出ます。

モデルは `avply.exe` と同じフォルダの `model/` に保存します。
初回だけの作業で、次からは待たずに字幕が出ます。

ダウンロードの途中で `S` キーを押すと中断します。
次にもう一度 `S` キーを押すと、続きから再開します。

## GPU の自動利用

Vulkan に対応した GPU ドライバがあれば、認識に GPU を自動で使います。
GPU が無い環境では CPU で動きます。設定の切り替えは不要です。

どちらで動いているかは、`avply.exe` と同じフォルダの `avply.log` で確認できます。
`WhisperEngine: backends=` で始まる行に、使えるエンジンの名前が並びます。
`Vulkan` があれば GPU を使っています。`CPU` だけなら GPU は使っていません。

GPU が使われないときは、GPU ドライバを最新版に更新してください。

## モデルの変更

既定のモデルは `ggml-large-v3-turbo-q5_0.bin`（約 574MB）です。
日本語の品質と速さのつり合いが良く、多くの環境ではこのままで困りません。

変えるときは、`avply.exe` と同じフォルダに `avply.local.toml` を作り、以下のように書きます。

```toml
[subtitle]
model = "ggml-small.bin"
```

avply を起動し直すと、新しいモデルを次の `S` キー押下でダウンロードします。

主なモデルは以下のとおりです。

| ファイル名 | サイズ | 向いている環境 |
| --- | --- | --- |
| `ggml-large-v3-turbo-q5_0.bin` | 約 574MB | 既定。品質と速さのつり合いが良い |
| `ggml-large-v3-turbo.bin` | 約 1.6GB | 量子化していない版。品質を最優先するとき |
| `ggml-medium.bin` | 約 1.5GB | GPU が無く、品質を優先するとき |
| `ggml-small.bin` | 約 490MB | GPU が無く、速さを優先するとき |

選べるモデルの一覧は [Hugging Face の whisper.cpp リポジトリ](https://huggingface.co/ggerganov/whisper.cpp/tree/main) にあります。
新しい形式のモデルを使うには、avply 側の更新が必要な場合があります。

すでに手元にあるモデルを使うときは、フルパスで指定します。この場合はダウンロードしません。

```toml
[subtitle]
model = "D:/tools/whisper/models/ggml-large-v3-turbo.bin"
```

## うまくいかないとき

| 症状 | 原因 | 対処 |
| --- | --- | --- |
| `Subtitle:ERR` になる | ダウンロードや認識に失敗した | `avply.exe` と同じフォルダの `avply.log` の末尾を見る |
| 数字が増えず、字幕が出ない | CPU で大きなモデルを使っている | `ggml-small.bin` に変える |
| `Subtitle:N/A` のまま | 音声ファイル、または音声を含まない動画を開いている | 字幕は音声付きの動画だけが対象 |
| GPU があるのに `backends=CPU` だけ | GPU ドライバが Vulkan に対応していない | GPU ドライバを更新する |
