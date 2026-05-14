# Subtitle Extractor (C++17)

A command-line tool that extracts subtitles from any video — including a full
60-minute file — using one of three interchangeable strategies.

It's built deliberately as a learning project: the architecture and code style
showcase the kinds of things a C++ interviewer at a AAA studio, a defense
contractor, or NASA would ask you to explain.

---

## What it does

| Mode | What it extracts | Tools used | Speed (60 min) |
|------|------------------|------------|----------------|
| `embedded` | Subtitle track already inside the container (MKV / MP4 / MOV) | FFmpeg | seconds |
| `whisper`  | Speech-to-text from the audio (no subs needed) | whisper.cpp | minutes |
| `ocr`      | Burned-in subtitles baked into the pixels | FFmpeg + Tesseract | tens of minutes |

Output formats: **SRT** (standard), **VTT** (web), **TXT** (no timestamps).
You can request several at once.

---

## Architecture (this is the interview-worthy part)

```
┌─────────────────────────┐
│         main.cpp        │  hand-rolled CLI parser
└────────────┬────────────┘
             │ uses
             v
┌─────────────────────────┐
│   ExtractorFactory      │  Factory Pattern — picks Strategy at runtime
└────────────┬────────────┘
             │ creates one of:
             v
┌─────────────────────────┐
│   IExtractor (abstract) │  Strategy Pattern interface
└────────────┬────────────┘
             │
   ┌─────────┼──────────────────┐
   v         v                  v
EmbeddedExtractor   WhisperExtractor   OcrExtractor
   (FFmpeg)           (whisper.cpp)       (Tesseract)
             │
             v
┌─────────────────────────┐
│     SubtitleWriter      │  writes SRT / VTT / TXT
└─────────────────────────┘
```

### Design patterns used

- **Strategy Pattern** — `IExtractor` is the abstract base; `EmbeddedExtractor`,
  `WhisperExtractor`, `OcrExtractor` are interchangeable strategies. `main`
  doesn't know or care which one it's holding.
- **Factory Pattern** — `ExtractorFactory::create()` centralizes the
  "which subclass do I instantiate?" decision. Adding a fourth mode means
  touching exactly one file outside of the new class itself.
- **Parameter Object** — `FactoryOptions` groups all tunables into one
  struct so we don't have a 10-parameter constructor.
- **RAII** — `std::unique_ptr<IExtractor>`, `std::ofstream`, and
  `std::filesystem::remove_all` clean themselves up automatically.

### Why these matter in interviews

- "Open / Closed Principle" — open for extension (add a strategy),
  closed for modification (don't touch existing extractors).
- "Liskov Substitution" — any concrete extractor can stand in for
  `IExtractor*` and the program still works.
- "Single Responsibility" — `SubtitleWriter` only writes, `IExtractor`
  only extracts, `ProcessRunner` only spawns processes.

---

## Build instructions

### Step 1 — install dependencies

You'll need these on your `PATH` (the program shells out to them):

- **FFmpeg** — required for all three modes
- **whisper.cpp** — required for `--mode whisper`. Build it once from
  https://github.com/ggerganov/whisper.cpp and put the resulting
  `whisper-cli` (or older `main`) binary on your `PATH`. Also download
  a model file like `ggml-base.en.bin`.
- **Tesseract OCR** — required for `--mode ocr`

#### Windows

```powershell
# FFmpeg + Tesseract via Chocolatey or Scoop
choco install ffmpeg tesseract
# Or scoop install ffmpeg tesseract
```

#### Linux (Debian / Ubuntu)

```bash
sudo apt update
sudo apt install ffmpeg tesseract-ocr tesseract-ocr-eng cmake build-essential
```

#### macOS

```bash
brew install ffmpeg tesseract cmake
```

### Step 2 — build the C++ project

From the `subtitle_extractor/` directory:

```bash
cmake -B build -S .
cmake --build build --config Release
```

On Windows / Visual Studio the executable lands at
`build/Release/subtitle_extractor.exe`. On Linux/Mac it's
`build/subtitle_extractor`.

---

## Usage

```bash
# Embedded — pull a text subtitle track straight out of an MKV
subtitle_extractor movie.mkv movie_subs --mode embedded --format srt,vtt

# Whisper — transcribe a 60-minute lecture
subtitle_extractor lecture.mp4 lecture --mode whisper \
                   --model ggml-base.en.bin --format srt

# OCR — pull hardcoded subs out of a burned-in video at 1.5 fps
subtitle_extractor burned.mp4 captions --mode ocr \
                   --ocr-fps 1.5 --format srt,txt
```

Run `subtitle_extractor --help` for the full option list.

---

## Code map (where to find what)

```
subtitle_extractor/
├── CMakeLists.txt
├── README.md                       <- you are here
├── include/
│   ├── SubtitleEntry.h             POD struct: start ms, end ms, text
│   ├── IExtractor.h                Strategy interface
│   ├── EmbeddedExtractor.h         FFmpeg-based subtitle stream extractor
│   ├── WhisperExtractor.h          whisper.cpp speech-to-text wrapper
│   ├── OcrExtractor.h              FFmpeg + Tesseract OCR pipeline
│   ├── ExtractorFactory.h          Factory + Mode enum + FactoryOptions
│   ├── SubtitleWriter.h            SRT/VTT/TXT writer
│   ├── ProcessRunner.h             Cross-platform subprocess wrapper
│   └── Logger.h                    Minimal logging facility
└── src/
    └── *.cpp implementations of all of the above + main.cpp
```

---

## Interview prep notes — how this project answers likely questions

These are the exact questions you should expect, and where the codebase
provides the answer.

### AAA game studio (e.g. Riot, Epic, Ubisoft)

> "How would you design a system where the user can choose between
> several implementations of the same operation?"

Answer: **Strategy Pattern + Factory**, like `IExtractor` here.
Walk them through `ExtractorFactory::create()`.

> "Why a virtual destructor?"

Look at `IExtractor.h`. Without `virtual ~IExtractor() = default;`,
`delete basePtr;` would only run the base destructor and leak the
derived class's members. This is a *classic* gotcha.

> "How do you avoid resource leaks?"

RAII everywhere — `std::unique_ptr<IExtractor>`, `std::ofstream`
in `SubtitleWriter`, `fs::remove_all` for temp dirs.

### Generic tech-company SWE interview (e.g. Microsoft, Bloomberg)

> "Tell me about a project where you had to integrate with external systems."

Talk about `ProcessRunner`: it's a thin wrapper over `popen`/`_popen`
that abstracts the Windows/POSIX difference, captures combined
stdout+stderr, and surfaces a structured `ProcessResult`. Mention
**failure modes**: what if the binary isn't on PATH? What if it
returns 0 but produces no output (whisper.cpp does that sometimes)?
What does your code do? (Answer: throws `std::runtime_error` with a
helpful message.)

> "What's the complexity of your OCR step?"

Read `OcrExtractor.cpp`:
- Frame sampling is O(duration × fps).
- OCR is the bottleneck — roughly 100–300 ms per frame on a CPU.
- The run-length-encoding loop that merges consecutive identical
  frames is **O(N) time, O(1) extra space**.
- A production version would parallelize OCR over a thread pool —
  good chance to bring up `std::async`, `std::thread`, or a
  work-stealing queue.

### Air Force / defense systems programming

These shops care about **determinism**, **defensive coding**, and
**no surprises in production**:

- Every error path throws a *typed exception* with a clear message —
  no silent failures.
- Tempfiles are placed under `std::filesystem::temp_directory_path()`
  (cross-platform, no hard-coded `/tmp`).
- Unreachable code branches still throw (see end of
  `ExtractorFactory::create`) — this is straight out of NASA's
  *Power of Ten* rules: "All loops must have a fixed upper bound …
  All code paths must be reachable or defended against."
- `noexcept` is used on accessors that truly cannot throw.

### NASA-flavor questions

> "Talk me through how a 60-minute video with no subtitles becomes a
> 5,000-line SRT file."

1. `ffmpeg` decodes the audio stream and resamples it to 16 kHz mono.
   That's the input format Whisper was trained on.
2. `whisper.cpp` chunks the audio into ~30-second windows, runs each
   through the model (a transformer encoder–decoder), and emits text
   segments with timestamps.
3. The CLI emits a `.srt` file. Our `parseSrt` walks the file with
   a regex matching `HH:MM:SS,mmm --> HH:MM:SS,mmm`, then collects
   subsequent lines until a blank separator.
4. The vector of `SubtitleEntry` is handed to `SubtitleWriter` which
   re-serializes it in whichever format(s) the user asked for.

If they ask "what's the data structure choice?" — `std::vector` is
cache-friendly, supports cheap iteration, and we never need to
insert in the middle. If we *did* need timestamp-based lookup we'd
switch to `std::map<int64_t, SubtitleEntry>` or a sorted vector +
`std::lower_bound` (O(log N)).

---

## Data structures & algorithms cheat-sheet (you said you want reminders)

| Concept | Where in this code |
|---|---|
| `std::vector` as dynamic array | `entries` in every extractor |
| `std::unique_ptr` (RAII smart pointer) | `main.cpp` holds the strategy |
| Polymorphism via abstract base | `IExtractor` |
| Switch dispatch over enum | `ExtractorFactory::create` |
| Regex parsing | `parseSrt` in EmbeddedExtractor.cpp |
| Run-length encoding | OcrExtractor: collapse consecutive identical OCR text |
| Hash-free string comparison | `std::string` == in the OCR dedup loop |
| Cross-platform abstraction | `ProcessRunner` with `_popen` / `popen` |
| Exception-based error handling | every `extract()` and `write*()` |

---

## English / communication notes

A few phrasings you can reuse word-for-word in an interview:

- *"I went with the Strategy Pattern here because the mode is chosen
  at **runtime** from CLI arguments — that rules out templates and
  steers me toward virtual dispatch."*
- *"`ProcessRunner` is a **thin abstraction** over the platform's
  `popen` family. It captures the combined stdout/stderr stream so
  I get **deterministic** error reporting when an external tool
  fails."*
- *"I prefer **throwing typed exceptions** over returning error codes
  because it keeps the happy path readable. I catch them at `main`
  and translate to an exit code."*
- *"For the OCR step, the bottleneck is **per-frame Tesseract**
  latency. The natural next step is a **thread-pool** that processes
  frames in parallel. That would change my O(N) sequential merge
  loop into an O(N) merge over results queued in **timestamp order**."*

The pattern: *what + why + what's next*. Interviewers love that
triple.

---

## Known limitations (be honest in interviews)

- `EmbeddedExtractor` only works for **text-based** subtitle streams
  (SRT/ASS/MOV_TEXT). Bitmap-based PGS subtitles need OCR.
- `WhisperExtractor` requires the `whisper-cli` (or older `main`)
  binary built from whisper.cpp and a `.bin` model file on disk.
- `OcrExtractor` only crops the bottom of the frame. Movies with
  top-of-screen captions will need a different crop region.
- All three extractors **shell out** instead of linking the
  libraries directly (libavformat, libwhisper, libtesseract). A
  v2.0 would link them in-process for speed and better error
  reporting. We took the shell-out path because:
  (a) it's vastly easier to get building on any machine;
  (b) it keeps the C++ codebase focused on architecture, not glue.

---

## License

This is a personal learning project. Treat it as MIT-licensed.
