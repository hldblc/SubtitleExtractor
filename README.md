# Subtitle Extractor

Desktop app for pulling subtitles out of any video file. Three modes:

- **Embedded**: copy the existing subtitle stream from MKV/MP4 containers
- **Whisper**: transcribe audio using OpenAI Whisper via FFmpeg's built-in `whisper` filter
- **OCR**: read burned-in subtitles by running Tesseract on cropped frames

Outputs SRT, VTT, and plain TXT. Pick one, two, or all three at once.

Written in C++17 with a Qt 6 GUI. Cross-platform, but the polish is on Windows.

## Screenshot

*(screenshot coming once I take one)*

## Features

- Batch queue: drop several videos in, hit Extract, walk away
- Live progress bar driven by FFmpeg's actual `time=` output, not a fake spinner
- Cancel button that kills the in-flight subprocess immediately
- Auto-detect for `ggml-*.bin` Whisper models in the install folder (picks the largest, which is the most accurate)
- Auto-update check against GitHub Releases on launch
- Single-installer distribution via Inno Setup

## Tech stack

- C++17, MSVC 19.5 (Visual Studio 2026)
- Qt 6.11 (Widgets, Concurrent, Network)
- CMake 3.16+
- FFmpeg 8.1+ built with `--enable-whisper`
- Tesseract 5 (only for OCR mode)
- Inno Setup 6 for the Windows installer

## Build

Install Qt 6, FFmpeg, and Tesseract first. Both FFmpeg and Tesseract must be on `PATH`.

```powershell
cmake -B build -S . -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64"
cmake --build build --config Release
```

Produces `build/Release/subtitle_extractor_gui.exe` (the GUI) and `subtitle_extractor.exe` (the CLI).

To produce a single-file Windows installer:

```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer.iss
```

Output goes to `installer/SubtitleExtractor_Setup_v1.0.exe`.

## Usage

### GUI

1. Add videos to the queue (multi-select supported).
2. Pick a mode. For Whisper, the model field is filled in automatically if a `ggml-*.bin` file is in the install folder.
3. Pick an output folder and tick the formats you want.
4. Click **Extract**. The progress bar tracks the current file; the status line shows `[N/M]` overall.

### CLI

```bash
subtitle_extractor movie.mkv movie_out --mode embedded --format srt,vtt
subtitle_extractor lecture.mp4 lecture --mode whisper --model ggml-base.en.bin --format srt
subtitle_extractor burned.mp4 captions --mode ocr --ocr-fps 1.5 --format srt,txt
```

Run `subtitle_extractor --help` for the full option list.

## Project layout

```
subtitle_extractor/
├── CMakeLists.txt
├── installer.iss              Inno Setup script
├── include/                   Public headers
├── src/                       Implementation
├── resources/                 App icon + Qt resource bundle
└── installer/                 Setup.exe output (gitignored)
```

CMake builds three targets:

- `subext_lib`: static library with the extraction logic. No Qt dependency, so the CLI doesn't pull Qt in.
- `subtitle_extractor`: CLI executable.
- `subtitle_extractor_gui`: Qt GUI executable.

## Known limitations

- Embedded mode only handles text-based subtitle streams (SRT, ASS, MOV_TEXT). Bitmap PGS subs need OCR.
- OCR mode hardcodes a bottom-25% crop. Videos with top-positioned subtitles need a different crop region.
- FFmpeg and Tesseract are not bundled in the installer. They must be installed separately and on `PATH`.
- The update checker notifies the user but doesn't auto-install the new version. It opens the GitHub release page in the default browser instead.

## Roadmap

- Bundle FFmpeg and Tesseract in the installer for a real single-file deployment
- In-app download and auto-install for updates, with SHA256 verification
- Drag-and-drop file input
- Configurable OCR crop region
- Per-file save behavior on conflict (overwrite, skip, rename)

## License

MIT.
