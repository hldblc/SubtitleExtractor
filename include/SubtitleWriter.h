#pragma once
#include "SubtitleEntry.h"
#include <filesystem>
#include <vector>

namespace subext {

// Writes a vector of SubtitleEntry to disk in three different formats.
// Pure static utility — no state.
class SubtitleWriter {
public:
    // SRT: HH:MM:SS,mmm --> HH:MM:SS,mmm
    static void writeSrt(const std::vector<SubtitleEntry>& entries,
                          const std::filesystem::path& outPath);

    // WebVTT: same shape but uses '.' instead of ',' for ms separator
    // and prepends "WEBVTT" header.
    static void writeVtt(const std::vector<SubtitleEntry>& entries,
                          const std::filesystem::path& outPath);

    // Plain text — just the lines, no timestamps.
    static void writeTxt(const std::vector<SubtitleEntry>& entries,
                          const std::filesystem::path& outPath);
};

} // namespace subext
