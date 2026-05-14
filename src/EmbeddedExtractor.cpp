#include "EmbeddedExtractor.h"
#include "CancellationToken.h"
#include "ProcessRunner.h"
#include "Logger.h"

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

} // anonymous namespace

std::vector<SubtitleEntry>
EmbeddedExtractor::extract(const std::filesystem::path& videoPath) {
    namespace fs = std::filesystem;

    fs::path tmp = fs::temp_directory_path() / "subext_embedded.srt";
    std::error_code ec; fs::remove(tmp, ec);

    reportProgress(0);

    StreamingProcessOptions opts;
    opts.executable = "ffmpeg";
    opts.args = {
        "-y", "-i", videoPath.string(),
        "-map", "0:s:" + std::to_string(streamIndex_),
        "-c:s", "srt",
        tmp.string()
    };
    opts.cancel = cancellation();
    std::string buf;
    opts.onLine = [&buf](std::string_view sv) {
        buf.append(sv.data(), sv.size());
        buf += '\n';
    };

    log::info("Running ffmpeg to extract embedded subtitle stream...");
    auto r = ProcessRunner::runStreaming(opts);

    if (r.cancelled) {
        fs::remove(tmp, ec);
        throw std::runtime_error("Cancelled by user.");
    }

    reportProgress(70);

    if (r.exitCode != 0 || !fs::exists(tmp)) {
        throw std::runtime_error(
            "ffmpeg failed to extract embedded subtitles "
            "(is there a text-based subtitle stream at index " +
            std::to_string(streamIndex_) + "?)\nffmpeg output:\n" + buf);
    }

    std::ifstream f(tmp);
    std::stringstream ss; ss << f.rdbuf();
    auto entries = parseSrt(ss.str());

    fs::remove(tmp, ec);

    if (entries.empty()) {
        throw std::runtime_error(
            "Extracted SRT file was empty. The stream may be image-based "
            "(e.g. PGS) — try OCR mode instead.");
    }
    reportProgress(100);
    log::info("Extracted " + std::to_string(entries.size()) + " cues.");
    return entries;
}

} // namespace subext
