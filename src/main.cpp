// ===========================================================================
// subtitle_extractor — main entry point
//
// Demonstrates:
//   • Strategy Pattern (IExtractor + 3 concrete classes)
//   • Factory Pattern (ExtractorFactory)
//   • RAII (unique_ptr, ofstream, filesystem cleanup)
//   • Exception-based error handling
//   • Hand-rolled CLI argument parser
//
// Build: see CMakeLists.txt
// Run:   ./subtitle_extractor video.mp4 outBase --mode whisper --format srt
// ===========================================================================

#include "ExtractorFactory.h"
#include "SubtitleWriter.h"
#include "Logger.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace subext;

namespace {

void printUsage(const char* prog) {
    std::cout <<
"Subtitle Extractor v1.0\n"
"\n"
"USAGE:\n"
"  " << prog << " <video> <output_basename> [options]\n"
"\n"
"MODES (--mode):\n"
"  embedded   Extract embedded subtitle track via FFmpeg (default, fastest)\n"
"  whisper    Transcribe audio via whisper.cpp (speech-to-text)\n"
"  ocr        OCR burned-in (hardsubbed) subtitles via Tesseract\n"
"\n"
"OUTPUT FORMATS (--format, comma-separated):\n"
"  srt | vtt | txt           default: srt\n"
"\n"
"OPTIONS:\n"
"  --mode <m>                Extraction mode (see above)\n"
"  --format <f1,f2,...>      One or more output formats\n"
"  --stream <n>              Embedded subtitle stream index (default 0)\n"
"  --model <path>            whisper.cpp model file (default ggml-base.en.bin)\n"
"  --lang <code>             Whisper language code (default en)\n"
"  --ocr-fps <n>             OCR sample frames per second (default 2.0)\n"
"  --ocr-bottom <0..1>       OCR crop fraction from bottom (default 0.25)\n"
"  --ocr-lang <lang>         Tesseract language (default eng)\n"
"  --verbose                 Verbose logging\n"
"  -h, --help                This help\n"
"\n"
"EXAMPLES:\n"
"  " << prog << " movie.mkv movie_subs --mode embedded --format srt,vtt\n"
"  " << prog << " lecture.mp4 lecture --mode whisper --model ggml-base.en.bin\n"
"  " << prog << " burned.mp4 captions --mode ocr --ocr-fps 1.5 --format srt,txt\n";
}

// Split a CSV-ish string into pieces. Hand-written because <ranges>
// isn't universally available everywhere, and writing tiny string utils
// is exactly the kind of thing interviewers will ask you to do live.
std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == d) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Args {
    std::filesystem::path videoPath;
    std::filesystem::path outBase;
    std::string mode = "embedded";
    std::vector<std::string> formats = { "srt" };
    FactoryOptions opts;
    bool help = false;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error(std::string("Missing value for ") + name);
            return argv[++i];
        };

        if (s == "-h" || s == "--help")  { a.help = true; }
        else if (s == "--mode")          { a.mode = next("--mode"); }
        else if (s == "--format")        { a.formats = split(next("--format"), ','); }
        else if (s == "--stream")        { a.opts.embeddedStreamIndex = std::stoi(next("--stream")); }
        else if (s == "--model")         { a.opts.whisperModelPath = next("--model"); }
        else if (s == "--lang")          { a.opts.whisperLanguage = next("--lang"); }
        else if (s == "--ocr-fps")       { a.opts.ocrSampleFps = std::stod(next("--ocr-fps")); }
        else if (s == "--ocr-bottom")    { a.opts.ocrBottomFrac = std::stod(next("--ocr-bottom")); }
        else if (s == "--ocr-lang")      { a.opts.ocrLang = next("--ocr-lang"); }
        else if (s == "--verbose")       { log::set_verbose(true); }
        else if (!s.empty() && s[0] == '-') {
            throw std::runtime_error("Unknown option: " + s);
        } else {
            positional.push_back(s);
        }
    }

    if (a.help) return a;
    if (positional.size() < 2)
        throw std::runtime_error(
            "Need <video> and <output_basename>. Use --help for usage.");

    a.videoPath = positional[0];
    a.outBase   = positional[1];
    a.opts.mode = ExtractorFactory::parseMode(a.mode);
    return a;
}

} // anonymous namespace

int main(int argc, char** argv) {
    try {
        Args a = parseArgs(argc, argv);
        if (a.help) { printUsage(argv[0]); return 0; }

        if (!std::filesystem::exists(a.videoPath)) {
            log::error("Video file not found: " + a.videoPath.string());
            return 2;
        }

        log::info("Input  : " + a.videoPath.string());
        log::info("Mode   : " + a.mode);
        log::info("Output : " + a.outBase.string() + ".{srt|vtt|txt}");

        // ----- The Strategy in action -------------------------------------
        std::unique_ptr<IExtractor> extractor =
            ExtractorFactory::create(a.opts);
        log::info("Strategy: " + extractor->name());

        std::vector<SubtitleEntry> entries =
            extractor->extract(a.videoPath);

        if (entries.empty()) {
            log::warn("No subtitle entries produced.");
            return 1;
        }

        // ----- Fan out to every requested format --------------------------
        for (const auto& fmt : a.formats) {
            std::filesystem::path out = a.outBase;
            out += "." + fmt;
            if      (fmt == "srt") SubtitleWriter::writeSrt(entries, out);
            else if (fmt == "vtt") SubtitleWriter::writeVtt(entries, out);
            else if (fmt == "txt") SubtitleWriter::writeTxt(entries, out);
            else { log::warn("Unknown format ignored: " + fmt); continue; }
            log::info("Wrote: " + out.string());
        }
        return 0;
    }
    catch (const std::exception& ex) {
        log::error(ex.what());
        return 1;
    }
}
