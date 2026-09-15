---
title: Setting up whisper-cli for subtitles
---

[![日本語](https://img.shields.io/badge/lang-日本語-red)](../whisper-setup.md)
[![English](https://img.shields.io/badge/lang-English-blue)](whisper-setup.md)

# Setting up whisper-cli for subtitles

avply creates subtitles by running the speech recognition tool whisper-cli (whisper.cpp) as an external program.
whisper-cli is not bundled with avply, so it has to be installed separately.
This page walks through preparing whisper-cli and a model file so that avply can use them.

## What you need

| Item | Description |
| --- | --- |
| whisper-cli | The whisper.cpp executable. There are GPU and CPU builds |
| Model file | Trained data for speech recognition (`ggml-*.bin`). A few hundred MB to 1.6GB |
| NVIDIA driver | Only for the GPU build. Installing the CUDA Toolkit is not required |

## 1. Check whether you have a GPU

If you have an NVIDIA GPU, use the GPU build. Recognition runs faster than real time, so subtitles catch up quickly.
Open PowerShell and run the following command.

```powershell
nvidia-smi
```

If a table with the GPU name and driver version appears, you can use the GPU build.
If you get an error such as "not recognized", go on with the CPU build.

## 2. Install whisper-cli

### GPU build (recommended)

Download the ZIP file from the following URL.

```
https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.2/whisper-cublas-12.4.0-bin-x64.zip
```

Extract the ZIP to `C:\tools\whisper\`.
After extracting, make sure that `C:\tools\whisper\Release\whisper-cli.exe` exists.
Use a path with only ASCII letters and digits. whisper-cli cannot open paths that contain other characters.

### CPU build

If Scoop is available, the following command installs it.

```powershell
scoop install whisper-cpp
```

avply finds whisper-cli installed by Scoop automatically, so no path setting is needed.
The CPU build is slow, so pick a small model in the next step.

## 3. Install a model file

Pick one model file from the following.

| File name | Size | Suited for |
| --- | --- | --- |
| `ggml-large-v3-turbo.bin` | about 1.6GB | GPU build. Recommended for practical quality |
| `ggml-medium.bin` | about 1.5GB | CPU build when quality matters more |
| `ggml-small.bin` | about 490MB | CPU build when speed matters more |

Create the folder `C:\tools\whisper\models\`. Then run the following in PowerShell to download.
Replace the file name with the one you picked.

```powershell
mkdir C:\tools\whisper\models
curl.exe -L -o C:\tools\whisper\models\ggml-large-v3-turbo.bin https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin
```

Use a path with only ASCII letters and digits for the model file as well.

## 4. Tell avply where they are

Create a file named `avply.local.toml` in the same folder as `avply.exe`.
Open it with Notepad or a similar editor, write the following, and save. Adjust the paths to your environment.

```toml
[subtitle]
whisper_path = "C:/tools/whisper/Release/whisper-cli.exe"
model = "C:/tools/whisper/models/ggml-large-v3-turbo.bin"
```

You can write path separators as `/`.
If you installed the CPU build with Scoop, you can omit the `whisper_path` line.

## 5. Check that it works

Restart avply, because settings are read only at startup.
Then open a video file.
Finally, press the `S` key.

The display at the bottom changes to `Subtitle:0%` and the number goes up.
When it changes to `Subtitle:ON`, subtitles appear at the bottom of the picture.
The next time you open the same video, subtitles appear immediately from the cache.

## Troubleshooting

| Symptom | Cause | What to do |
| --- | --- | --- |
| `Subtitle:N/A` stays after pressing `S` | The path to whisper-cli or the model file is wrong | Check the paths in `avply.local.toml` and restart avply |
| `Subtitle:ERR` appears | whisper-cli exited with an error | Look at the end of `avply.log` next to `avply.exe` |
| The number does not go up and no subtitles appear | A large model on the CPU build | Switch to `ggml-small.bin` |
| `avply.log` reports a missing DLL | The NVIDIA driver is old | Update the driver |
