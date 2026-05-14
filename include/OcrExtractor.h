#pragma once
#include "IExtractor.h"
#include <string>

namespace subext {

// Strategy #3 — for "hardsubs" (subtitles burned into the pixels).
// Pipeline:
//   1. ffmpeg samples N frames per second, cropping to the bottom region
//      where subtitles typically live, and converts each frame to gray.
//   2. tesseract OCR converts each cropped frame to text.
//   3. We walk the time-ordered frames and merge consecutive frames
//      whose text is identical into a single SubtitleEntry.
//
// COMPLEXITY (interview material):
//   Frames produced  = duration_seconds * sampleFps
//   For 60 min @ 2 fps = 7200 frames. OCR per frame is the bottleneck
//   (≈ 100–300 ms each on a CPU), so total ≈ 12–36 minutes single-threaded.
//   A real production version would parallelize OCR across cores.
//
// DSA NOTE: The "merge consecutive duplicates" step is the same idea as
// "run-length encoding" — a classic warm-up algorithm problem.
class OcrExtractor : public IExtractor {
public:
    OcrExtractor(double sampleFps    = 2.0,
                 double bottomFrac   = 0.25,
                 std::string ocrLang = "eng")
        : sampleFps_(sampleFps)
        , bottomFrac_(bottomFrac)
        , ocrLang_(std::move(ocrLang)) {}

    std::vector<SubtitleEntry>
    extract(const std::filesystem::path& videoPath) override;

    std::string name() const override { return "OCR (Tesseract)"; }

private:
    double      sampleFps_;
    double      bottomFrac_;
    std::string ocrLang_;
};

} // namespace subext
