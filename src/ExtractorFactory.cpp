#include "ExtractorFactory.h"
#include "EmbeddedExtractor.h"
#include "WhisperExtractor.h"
#include "OcrExtractor.h"
#include <stdexcept>

namespace subext {

std::unique_ptr<IExtractor>
ExtractorFactory::create(const FactoryOptions& opts) {
    switch (opts.mode) {
    case Mode::Embedded:
        return std::make_unique<EmbeddedExtractor>(opts.embeddedStreamIndex);
    case Mode::Whisper:
        return std::make_unique<WhisperExtractor>(
            opts.whisperModelPath, opts.whisperLanguage,
            opts.whisperTranslate);
    case Mode::Ocr:
        return std::make_unique<OcrExtractor>(
            opts.ocrSampleFps, opts.ocrBottomFrac, opts.ocrLang);
    }
    // Unreachable in a well-formed program, but defensive coding wins
    // points in interviews and is a NASA "Power of Ten" rule
    // (defend against the impossible).
    throw std::runtime_error("ExtractorFactory: unknown Mode value");
}

Mode ExtractorFactory::parseMode(const std::string& s) {
    if (s == "embedded") return Mode::Embedded;
    if (s == "whisper")  return Mode::Whisper;
    if (s == "ocr")      return Mode::Ocr;
    throw std::runtime_error(
        "Invalid mode '" + s + "' — use embedded | whisper | ocr");
}

} // namespace subext
