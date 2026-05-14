#include "SubtitleWriter.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace subext {

namespace {

// "HH:MM:SS,mmm" — SRT style
std::string fmtSrt(std::int64_t ms) {
    if (ms < 0) ms = 0;
    std::int64_t h  = ms / 3600000; ms %= 3600000;
    std::int64_t m  = ms / 60000;   ms %= 60000;
    std::int64_t s  = ms / 1000;    ms %= 1000;
    std::ostringstream o;
    o << std::setfill('0') << std::setw(2) << h << ':'
      <<                       std::setw(2) << m << ':'
      <<                       std::setw(2) << s << ','
      <<                       std::setw(3) << ms;
    return o.str();
}

// VTT differs only in ',' -> '.'
std::string fmtVtt(std::int64_t ms) {
    std::string s = fmtSrt(ms);
    s[s.size() - 4] = '.';
    return s;
}

void openOrThrow(std::ofstream& f, const std::filesystem::path& p) {
    f.open(p);
    if (!f) throw std::runtime_error("Cannot open for writing: " + p.string());
}

} // anonymous namespace

void SubtitleWriter::writeSrt(const std::vector<SubtitleEntry>& entries,
                                const std::filesystem::path& outPath) {
    std::ofstream f; openOrThrow(f, outPath);
    int idx = 1;
    for (const auto& e : entries) {
        f << idx++ << '\n'
          << fmtSrt(e.startMs) << " --> " << fmtSrt(e.endMs) << '\n'
          << e.text << "\n\n";
    }
}

void SubtitleWriter::writeVtt(const std::vector<SubtitleEntry>& entries,
                                const std::filesystem::path& outPath) {
    std::ofstream f; openOrThrow(f, outPath);
    f << "WEBVTT\n\n";
    for (const auto& e : entries) {
        f << fmtVtt(e.startMs) << " --> " << fmtVtt(e.endMs) << '\n'
          << e.text << "\n\n";
    }
}

void SubtitleWriter::writeTxt(const std::vector<SubtitleEntry>& entries,
                                const std::filesystem::path& outPath) {
    std::ofstream f; openOrThrow(f, outPath);
    for (const auto& e : entries) {
        f << e.text << '\n';
    }
}

} // namespace subext
