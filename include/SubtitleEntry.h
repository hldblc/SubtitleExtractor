#pragma once
#include <cstdint>
#include <string>

namespace subext {

// A single subtitle "cue" — one line of text with a start/end time.
// Times are stored in milliseconds since the start of the video.
//
// DESIGN NOTE: This is a Plain-Old-Data (POD) style struct. We keep it
// dumb on purpose — all behavior lives in extractors and writers.
// In interviews, this is a "Data Transfer Object" (DTO) pattern.
struct SubtitleEntry {
    std::int64_t startMs{0};
    std::int64_t endMs{0};
    std::string  text;

    // noexcept tells the compiler this cannot throw — enables optimizations
    // and is good practice for trivial accessors.
    bool isValid() const noexcept {
        return endMs > startMs && !text.empty();
    }
};

} // namespace subext
