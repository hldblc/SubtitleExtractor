#pragma once
#include "IExtractor.h"

namespace subext {

// Strategy #1 — pulls a subtitle track that's already embedded inside
// the video container (MKV, MP4, MOV, etc.). Calls ffmpeg as a child
// process. This is the fastest extractor since no AI/OCR is involved.
class EmbeddedExtractor : public IExtractor {
public:
    explicit EmbeddedExtractor(int streamIndex = 0)
        : streamIndex_(streamIndex) {}

    std::vector<SubtitleEntry>
    extract(const std::filesystem::path& videoPath) override;

    std::string name() const override { return "Embedded (FFmpeg)"; }

private:
    int streamIndex_;   // which subtitle track to grab (0 = first)
};

} // namespace subext
