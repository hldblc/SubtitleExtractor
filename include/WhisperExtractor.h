#pragma once
#include "IExtractor.h"
#include <string>

namespace subext {

// Strategy #2 — runs the audio of the video through whisper.cpp
// (a high-quality C++ port of OpenAI's Whisper model). Works on any
// video even if it has zero embedded subtitle tracks. For a 60 minute
// video and a `base` model, this typically runs in ~5–15 minutes on
// a modern CPU.
class WhisperExtractor : public IExtractor {
public:
    WhisperExtractor(std::string modelPath,
                     std::string language = "en",
                     bool        translate = false)
        : modelPath_(std::move(modelPath))
        , language_(std::move(language))
        , translate_(translate) {}

    std::vector<SubtitleEntry>
    extract(const std::filesystem::path& videoPath) override;

    std::string name() const override { return "Whisper (whisper.cpp)"; }

private:
    std::string modelPath_;
    std::string language_;
    bool        translate_;
};

} // namespace subext
