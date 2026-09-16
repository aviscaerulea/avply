---
title: Subtitle models and GPU
---

[![日本語](https://img.shields.io/badge/lang-日本語-red)](../whisper-setup.md)
[![English](https://img.shields.io/badge/lang-English-blue)](whisper-setup.md)

# Subtitle models and GPU

avply has a speech recognition engine built in, so there is no other software to install.
All it needs is a speech recognition model, and avply downloads that for you.
This page explains how that works, how to pick a model, and how the GPU is used.

## It works out of the box

Open a video and press the `S` key. A dialog asks whether to download the model.
Choose Yes and the download starts, with progress shown at the bottom of the window as `Subtitle:DL 42%`.
When the download finishes, recognition starts and the number climbs from `Subtitle:0%`.
Once it changes to `Subtitle:ON`, subtitles appear at the bottom of the picture.

The model is saved to the `model/` folder next to `avply.exe`.
This happens only the first time; after that subtitles appear without waiting.

Pressing `S` during the download cancels it.
Pressing `S` again later resumes from where it stopped.

## The GPU is used automatically

If your GPU driver supports Vulkan, recognition runs on the GPU automatically.
Without a GPU it runs on the CPU. There is no setting to switch.

To see which one is in use, open `avply.log` next to `avply.exe`.
The line starting with `WhisperEngine: backends=` lists the available engines.
`Vulkan` means the GPU is in use. Only `CPU` means it is not.

If the GPU is not used, update your GPU driver to the latest version.

## Changing the model

The default model is `ggml-large-v3-turbo-q5_0.bin` (about 574MB).
It balances quality and speed well, and most setups need nothing else.

To change it, create `avply.local.toml` next to `avply.exe` and write the following.

```toml
[subtitle]
model = "ggml-small.bin"
```

Restart avply, and the next press of `S` downloads the new model.

The main models are as follows.

| File name | Size | Suited for |
| --- | --- | --- |
| `ggml-large-v3-turbo-q5_0.bin` | about 574MB | Default. Good balance of quality and speed |
| `ggml-large-v3-turbo.bin` | about 1.6GB | Not quantized. When quality matters most |
| `ggml-medium.bin` | about 1.5GB | No GPU, quality over speed |
| `ggml-small.bin` | about 490MB | No GPU, speed over quality |

The full list of models is on the [whisper.cpp repository at Hugging Face](https://huggingface.co/ggerganov/whisper.cpp/tree/main).
Models in a newer format may need an update to avply before they work.

To use a model you already have, give its full path. Nothing is downloaded in that case.

```toml
[subtitle]
model = "D:/tools/whisper/models/ggml-large-v3-turbo.bin"
```

## Troubleshooting

| Symptom | Cause | What to do |
| --- | --- | --- |
| `Subtitle:ERR` appears | The download or the recognition failed | Look at the end of `avply.log` next to `avply.exe` |
| The number does not climb and no subtitles appear | A large model on the CPU | Switch to `ggml-small.bin` |
| `Subtitle:N/A` stays | An audio file, or a video with no audio, is open | Subtitles only work on videos that have audio |
| `backends=CPU` only, although a GPU is present | The GPU driver does not support Vulkan | Update the GPU driver |
