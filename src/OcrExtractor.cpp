#include "OcrExtractor.h"
#include "CancellationToken.h"
#include "ProcessRunner.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace subext {

namespace {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string normalizeWs(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool lastWasSpace = true;
    for (char c : in) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) { out += ' '; lastWasSpace = true; }
        } else {
            out += c;
            lastWasSpace = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string runTesseract(const std::filesystem::path& img,
                         const std::string& lang,
                         CancellationToken* cancel) {
    StreamingProcessOptions opts;
    opts.executable = "tesseract";
    opts.args  = { img.string(), "stdout", "-l", lang, "--psm", "6" };
    opts.cancel = cancel;
    std::string buf;
    opts.onLine = [&buf](std::string_view sv) {
        buf.append(sv.data(), sv.size());
        buf += '\n';
    };
    auto r = ProcessRunner::runStreaming(opts);
    if (r.cancelled) return std::string{};
    return normalizeWs(trim(buf));
}

} // anonymous namespace

std::vector<SubtitleEntry>
OcrExtractor::extract(const std::filesystem::path& videoPath) {
    namespace fs = std::filesystem;

    fs::path tmpDir = fs::temp_directory_path() / "subext_ocr_frames";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    char filter[256];
    std::snprintf(filter, sizeof(filter),
        "crop=in_w:in_h*%.3f:0:in_h*%.3f,fps=%.3f,format=gray",
        bottomFrac_, 1.0 - bottomFrac_, sampleFps_);

    fs::path pattern = tmpDir / "frame_%06d.png";

    log::info("Sampling cropped frames with ffmpeg...");
    reportProgress(0);

    StreamingProcessOptions ffOpts;
    ffOpts.executable = "ffmpeg";
    ffOpts.args = {
        "-y", "-i", videoPath.string(),
        "-vf", filter,
        pattern.string()
    };
    ffOpts.cancel = cancellation();
    ffOpts.onLine = [](std::string_view) { /* ignored — coarse 0-10% range */ };
    auto r = ProcessRunner::runStreaming(ffOpts);
    if (r.cancelled) {
        fs::remove_all(tmpDir, ec);
        throw std::runtime_error("Cancelled by user.");
    }
    if (r.exitCode != 0) {
        throw std::runtime_error("ffmpeg frame sampling failed.");
    }
    reportProgress(10);

    std::vector<fs::path> frames;
    for (const auto& entry : fs::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".png") {
            frames.push_back(entry.path());
        }
    }
    std::sort(frames.begin(), frames.end());

    log::info("Running OCR on " + std::to_string(frames.size()) +
              " frames (this is the slow part)...");

    std::vector<SubtitleEntry> entries;
    std::string  currentText;
    std::int64_t currentStartMs = 0;
    int          frameIdx       = 0;
    const int    totalFrames    = static_cast<int>(frames.size());

    auto frameToMs = [this](int idx) {
        return static_cast<std::int64_t>((idx / sampleFps_) * 1000.0);
    };

    auto closeCue = [&](std::int64_t endMs) {
        if (!currentText.empty()) {
            SubtitleEntry e;
            e.startMs = currentStartMs;
            e.endMs   = endMs;
            e.text    = currentText;
            if (e.isValid()) entries.push_back(std::move(e));
        }
    };

    for (const auto& frame : frames) {
        if (isCancelled()) {
            fs::remove_all(tmpDir, ec);
            throw std::runtime_error("Cancelled by user.");
        }
        std::string text = runTesseract(frame, ocrLang_, cancellation());

        if (text != currentText) {
            closeCue(frameToMs(frameIdx));
            currentText    = text;
            currentStartMs = frameToMs(frameIdx);
        }
        ++frameIdx;

        if (totalFrames > 0) {
            // Map frames done into [10, 100] percent.
            int pct = 10 + (frameIdx * 90) / totalFrames;
            reportProgress(pct);
        }

        if ((frameIdx % 200) == 0) {
            log::info("...processed " + std::to_string(frameIdx) +
                      "/" + std::to_string(totalFrames) + " frames");
        }
    }
    closeCue(frameToMs(frameIdx));

    fs::remove_all(tmpDir, ec);
    reportProgress(100);
    log::info("OCR produced " + std::to_string(entries.size()) + " cues.");
    return entries;
}

} // namespace subext
