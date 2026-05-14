#pragma once
#include "IExtractor.h"
#include <memory>
#include <string>

namespace subext {

// Selects which Strategy gets instantiated.
enum class Mode { Embedded, Whisper, Ocr };

// All tunables grouped in one struct so we don't have a 10-parameter
// constructor (parameter object pattern — refactoring 101).
struct FactoryOptions {
    Mode mode{Mode::Embedded};

    // Embedded options
    int embeddedStreamIndex{0};

    // Whisper options
    std::string whisperModelPath{"ggml-base.en.bin"};
    std::string whisperLanguage{"en"};
    bool        whisperTranslate{false};  // translate audio -> English text

    // OCR options
    double      ocrSampleFps{2.0};   // frames per second to sample
    double      ocrBottomFrac{0.25}; // crop this fraction from the bottom
    std::string ocrLang{"eng"};      // tesseract language code
};

// =======================================================================
// FACTORY PATTERN
// =======================================================================
// Centralizes the "which concrete class do I create?" decision in one
// spot. If you ever add a new extraction strategy (say, a cloud-API
// transcription backend), you only touch the factory — main.cpp keeps
// holding IExtractor*, oblivious.
// =======================================================================
class ExtractorFactory {
public:
    static std::unique_ptr<IExtractor> create(const FactoryOptions& opts);
    static Mode parseMode(const std::string& s);
};

} // namespace subext
