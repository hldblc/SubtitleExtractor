#include "WhisperExtractor.h"
#include "CancellationToken.h"
#include "ProcessRunner.h"
#include "Logger.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace subext {

namespace {

std::int64_t parseSrtTime(const std::string& s) {
    int h{}, m{}, sec{}, ms{};
    if (std::sscanf(s.c_str(), "%d:%d:%d,%d", &h, &m, &sec, &ms) != 4)
        return -1;
    return (static_cast<std::int64_t>(h) * 3600 + m * 60 + sec) * 1000 + ms;
}

std::vector<SubtitleEntry> parseSrt(const std::string& content) {
    std::vector<SubtitleEntry> out;
    std::istringstream iss(content);
    std::string line;
    static const std::regex timeRe(
        R"((\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3}))");
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, timeRe)) {
            SubtitleEntry e;
            e.startMs = parseSrtTime(m[1]);
            e.endMs   = parseSrtTime(m[2]);
            std::string text;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;
                if (!text.empty()) text += '\n';
                text += line;
            }
            e.text = std::move(text);
            if (e.isValid()) out.push_back(std::move(e));
        }
    }
    return out;
}

std::string toForwardSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

// Parse "HH:MM:SS.mm" (FFmpeg's progress time format) -> milliseconds.
// Returns -1 on parse failure.
std::int64_t parseFfmpegTime(const std::string& s) {
    int h{}, m{}, sec{}, cs{};
    if (std::sscanf(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &cs) < 3)
        return -1;
    return (static_cast<std::int64_t>(h) * 3600 + m * 60 + sec) * 1000
         + cs * 10; // FFmpeg's centiseconds -> ms
}

} // anonymous namespace

std::vector<SubtitleEntry>
WhisperExtractor::extract(const std::filesystem::path& videoPath) {
    namespace fs = std::filesystem;

    fs::path outSrt = fs::temp_directory_path() / "subext_whisper.srt";
    std::error_code ec;
    fs::remove(outSrt, ec);

    const std::string modelFs = toForwardSlashes(modelPath_);
    const std::string outFs   = toForwardSlashes(outSrt.string());

    auto esc = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '\\' || c == ':' || c == '\'') out += '\\';
            out += c;
        }
        return out;
    };

    const std::string filter =
        "whisper=model="  + esc(modelFs)
      + ":language="      + language_
      + ":destination="   + esc(outFs)
      + ":format=srt"
      + ":queue=10";

    log::info("Running ffmpeg with built-in whisper filter (transcribing)...");
    log::debug("Filter: " + filter);

    // ------- Progress parsing state -------
    // FFmpeg writes "Duration: HH:MM:SS.mm" once near the top of stderr,
    // then "time=HH:MM:SS.mm" repeatedly during processing. We compute
    // percent = currentMs / totalMs * 100.
    std::int64_t totalMs = 0;
    static const std::regex durRe(R"(Duration:\s*(\d+:\d+:\d+\.\d+))");
    static const std::regex timeRe(R"(time=(\d+:\d+:\d+\.\d+))");

    StreamingProcessOptions popts;
    popts.executable = "ffmpeg";
    popts.args = {
        "-y",
        "-i",  videoPath.string(),
        "-vn",
        "-af", filter,
        "-f",  "null",
        "-"
    };
    popts.cancel = cancellation();
    popts.onLine = [&](std::string_view sv) {
        std::string line(sv);
        std::smatch m;
        if (totalMs == 0 && std::regex_search(line, m, durRe)) {
            totalMs = parseFfmpegTime(m[1]);
        } else if (totalMs > 0 && std::regex_search(line, m, timeRe)) {
            std::int64_t curMs = parseFfmpegTime(m[1]);
            if (curMs >= 0) {
                int pct = static_cast<int>(curMs * 100 / totalMs);
                reportProgress(pct);
            }
        }
    };

    auto r = ProcessRunner::runStreaming(popts);

    if (r.cancelled) {
        fs::remove(outSrt, ec);
        throw std::runtime_error("Cancelled by user.");
    }

    if (!fs::exists(outSrt)) {
        throw std::runtime_error(
            "ffmpeg whisper filter did not produce SRT output.\n"
            "Verify:\n"
            "  1. FFmpeg was built with --enable-whisper\n"
            "  2. Model file exists at: " + modelPath_ + "\n"
            "  3. Audio decoded successfully\n");
    }

    std::ifstream f(outSrt);
    if (!f) {
        throw std::runtime_error("Cannot read produced SRT: " + outSrt.string());
    }
    std::stringstream ss; ss << f.rdbuf();
    auto entries = parseSrt(ss.str());

    fs::remove(outSrt, ec);

    if (entries.empty()) {
        throw std::runtime_error(
            "SRT file was created but no valid entries were parsed.");
    }
    reportProgress(100);
    log::info("Transcribed " + std::to_string(entries.size()) + " segments.");
    return entries;
}

} // namespace subext
