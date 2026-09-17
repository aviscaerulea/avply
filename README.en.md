# avply

[![日本語](https://img.shields.io/badge/lang-日本語-red)](README.md)
[![English](https://img.shields.io/badge/lang-English-blue)](README.en.md)
[![Release](https://img.shields.io/github/v/release/aviscaerulea/avply)](https://github.com/aviscaerulea/avply/releases/latest)
[![License](https://img.shields.io/github/license/aviscaerulea/avply)](LICENSE)
[![Build](https://github.com/aviscaerulea/avply/actions/workflows/release.yml/badge.svg)](https://github.com/aviscaerulea/avply/actions/workflows/release.yml)

avply is a media player for reviewing meeting recordings quickly, clearly, and only where it matters.
Faster playback and voice enhancement cut down the time you spend watching, and you can pull out just the part you need.
It starts up light, so it also works well as an everyday video and audio player.

![avply screenshot](docs/images/screenshot.png)

## Features

- Fast startup: little delay between launching and playing
- Speed control: change playback speed in 0.05 steps while keeping the original pitch
  (kept across files)
- Voice enhancement: automatically evens out volume differences between speakers
  and lifts quiet remarks
- Subtitles: recognizes speech with whisper and overlays subtitles on the video during playback
- Output device follow: switches the output when the OS default audio device changes,
  even during playback
- Seek bar preview: shows a thumbnail and timestamp for the position under the cursor
- Waveform display: draws the waveform of the loaded audio over the seek bar
- Fast trimming: cuts the selected range at keyframe boundaries without re-encoding
- Conversion: re-encodes video to AV1 + Opus and audio to Opus

### Supported file formats

| Type | Extensions |
| --- | --- |
| Video | mp4, mkv, mov, avi, webm |
| Audio | mp3, wav, flac, ogg, opus |

When you load an audio file, the window switches to a compact layout without the preview area.

### Voice enhancement

In meeting recordings, remarks made far from the microphone sound quiet while nearby ones sound loud.
Voice enhancement combines noise suppression and automatic gain control to even out that difference during playback.
The C key or the item in the right-click menu toggles it on and off.
It always starts off at launch, and the setting is not saved.

### Subtitles

The audio of a video is recognized with whisper.cpp, and subtitles are overlaid at the bottom of the picture.
The S key or the item in the right-click menu toggles it on and off.
It always starts off at launch, and the setting is not saved. It stays on when you switch files.

Recognition proceeds from the beginning, and subtitles appear once it has passed the playback position.
If you seek to a position that has not been recognized yet, no subtitles appear until recognition catches up.
Results are cached per video, so they show without waiting from the second time on.
While recognition is running, the bottom of the window shows the progress like `Subtitle:42%`, and it changes to `Subtitle:ON` when done.
If it fails, it shows `Subtitle:ERR`.

Speech recognition needs a model file. The first time you turn subtitles on, avply asks and then downloads it.
While downloading, the bottom of the window shows `Subtitle:DL 42%`.
A GPU is used automatically when available, and the CPU otherwise.
Audio files are not supported, and the display shows `Subtitle:N/A` for them.
See [Subtitle models and GPU](https://aviscaerulea.github.io/avply/en/whisper-setup.html) for details.

## Installation

### Requirements

- Windows 11
- ffmpeg (installed separately; it is also used to read media information during playback)
- NVIDIA GPU (only needed for video conversion; AV1 NVENC support required,
  RTX 30 series or later recommended)
- A GPU driver with Vulkan support (only needed to speed up subtitle recognition on the GPU)

Trimming does not re-encode, so no GPU is required. Audio-only conversion also runs on the CPU.
Subtitles work on the CPU too, but a GPU recognizes speech far faster.

### Steps

#### From the release ZIP

Download `avply-<version>-x64.zip` from [Releases](https://github.com/aviscaerulea/avply/releases). Then extract the downloaded file. Finally, run `avply.exe`.
In that case, install ffmpeg separately (`scoop install ffmpeg` or an [official build](https://www.gyan.dev/ffmpeg/builds/)).

#### From Scoop

Recommended when Scoop is available. ffmpeg is pulled in as a dependency.

```powershell
scoop bucket add aviscaerulea https://github.com/aviscaerulea/scoop-bucket
scoop install aviscaerulea/avply
```

#### Uninstall

Even after removing the app, the settings changed from the right-click menu (such as always-on-top) remain in the registry under `HKCU\Software\avply`.
To remove them completely, delete that key with the Registry Editor.

## Usage

Detailed usage and the subtitle settings are on the [usage page](https://aviscaerulea.github.io/avply/en/).

### Loading a file

You can load a file in any of the following ways.

- "Open file" in the right-click menu
- Drag and drop onto the window
- Drag and drop onto `avply.exe`, or use "Send to" / "Open with" in Windows
- Run `avply.exe <media file>` from the command line

Loading the same file again restarts playback from the beginning.

### Keyboard and mouse

Playback controls are as follows.

| Action | Key | Mouse |
| --- | --- | --- |
| Play / pause | Space | Click the preview area (not available for audio-only files) |
| Seek | ← → | Drag the seek bar, or scroll over the seek bar or preview area |
| Large seek | Shift+← / Shift+→ | |
| Previous / next file in the folder | Alt+← / Alt+→ | |
| Playback speed ±0.05x | `.` faster / `,` slower | Ctrl+wheel |
| Volume ±0.05 | ↑ ↓ | Shift+wheel |
| Switch voice enhancement | C | |
| Switch subtitles | S | |
| Reset playback settings | G | |

Trimming controls are as follows.

| Action | Key | Mouse |
| --- | --- | --- |
| Set the start of the range | `[` | 【 button |
| Set the end of the range | `]` | 】 button |
| Clear the range only (playback position kept) | R | |
| Run / cancel trimming | | ✂ button |

The first press of G returns to neutral values (speed 1.00, volume 100%, voice enhancement off), and the second press restores the speed and volume from startup.
File switching follows the file-name order and stops at the first and last files in the folder.
The bottom of the window always shows the current playback speed, volume, voice enhancement, and subtitle state.

### Trimming and conversion

Select a range first, then run the trim.
The selected range is highlighted in red on the seek bar.
Because nothing is re-encoded, saving runs at close to disk-copy speed.

Conversion runs from "Convert file" in the right-click menu.
Video is re-encoded to AV1 + Opus, and audio to Opus at 96kbps.
Video wider than 2048px is scaled down automatically while keeping its aspect ratio.

### Output files

Output goes to the same folder as the input, named `<original name>_mod.<extension>`.
If that name already exists, a number is appended, as in `_mod2` or `_mod3`.
Processing a file that already has a `_mod` suffix overwrites the file with the same name.

| Mode | Input | Output extension |
| --- | --- | --- |
| Conversion | Video | `.mp4` (AV1 + Opus) |
| Conversion | Audio | `.opus` |
| Trimming | Video and audio | Same as the input |

## Configuration

Behavior is adjusted in `avply.toml`, located in the same folder as the executable.
To keep machine-specific values out of the repository, write the same keys in `avply.local.toml` in that folder; those values take precedence.
The main entries are listed below. Default values and valid ranges for each key are documented in the comments inside `avply.toml`.

| Section | Contents |
| --- | --- |
| `[ffmpeg]` | Path to ffmpeg.exe |
| `[seek]` | Seek amounts for arrow keys and the wheel |
| `[playback]` | Initial playback speed, hardware decoder priority |
| `[window]` | Maximum window size on load (ratio of the monitor) |
| `[audio]` | Initial volume, silence tone |
| `[subtitle]` | Subtitle model, download source, recognition language |

The ffmpeg path is resolved in this order: `path` under `[ffmpeg]`, the default Scoop location, then the `PATH` environment variable.
No configuration is needed if it is available through Scoop or `PATH`.
To set it explicitly, write it as follows.

```toml
[ffmpeg]
path = "C:/Users/yourname/scoop/apps/ffmpeg/current/bin/ffmpeg.exe"
```

The default subtitle model is downloaded automatically. To use another one, write its name in `model` under `[subtitle]`.

```toml
[subtitle]
model = "ggml-small.bin"
```

Always-on-top during playback, single-instance enforcement, and process priority are toggled from the settings in the right-click menu.
These are stored in the registry and kept for the next launch.

## Limitations

- Trimming cuts at keyframe boundaries, so the start position is rounded back
  from the one you specified
- With some formats such as Ogg/Opus, the playback position right after a trim can be off
  by a few tens of milliseconds
- Volume is capped at 100%; amplification beyond that is not supported
  (use voice enhancement to lift quiet remarks)
- Video conversion requires an NVIDIA GPU with AV1 NVENC support
- Where recognition cannot keep up with playback, stretches without subtitles continue
  (switch to a smaller model)
- The subtitle cache is keyed on the video content alone, so switching models still shows
  the previous recognition result

## Build

The following tools are required.

- Visual Studio 2026 Build Tools (C++ workload)
- CMake 3.25 or later
- Qt 6.10.3 MSVC2022 x64
- Vulkan SDK (required to build subtitle recognition with GPU support)

Qt can be installed with the following command.
Match the install location with `CMAKE_PREFIX_PATH` in `CMakePresets.json`.

```powershell
python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --outputdir <install folder> --modules qtmultimedia
```

Build with the following command.
The executable is generated at `out/Release/avply.exe`.

```powershell
pwsh.exe -File build.ps1
```

This build configures subtitle recognition with GPU support, the same as the released binaries. It therefore requires the Vulkan SDK.

If the Vulkan SDK is not available, configure with `AVPLY_WHISPER_VULKAN=OFF` using the command below. Subtitle recognition then runs on the CPU.
Keep the build directory separate from `out/`. If you configure into `out/`, the next `build.ps1` run reverts it to the GPU configuration.

```powershell
cmake -S . -B out-cpu -G "Visual Studio 18 2026" -A x64 `
    -DCMAKE_PREFIX_PATH="<same value as CMAKE_PREFIX_PATH in CMakePresets.json>" -DAVPLY_WHISPER_VULKAN=OFF
cmake --build out-cpu --config Release
```

## License

avply itself is distributed under the GNU LGPL v3.
See the bundled `LICENSE` (LGPL v3) and `COPYING` (GPL v3) for the full text.

License handling for the dependencies is as follows.

- Qt 6.10 (LGPL v3): linked dynamically as DLLs, so users can replace Qt by swapping
  in DLLs of the same name
- SoundTouch 2.4.0 (LGPL v2.1 or later): linked statically, so avply is licensed
  under LGPL v3 to preserve the right to relink
- WebRTC Audio Processing (BSD): linked statically; BSD is compatible with LGPL v3
  and adds no further redistribution obligations
- whisper.cpp (MIT): bundled as DLLs; MIT is compatible with LGPL v3 and adds no
  further redistribution obligations
- ffmpeg: invoked as an external process, so no linking relationship arises
