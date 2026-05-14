#pragma once
#include "SubtitleEntry.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace subext {

class CancellationToken;

// =======================================================================
// STRATEGY PATTERN INTERFACE
// =======================================================================
// Every extraction "strategy" (embedded / whisper / OCR) implements this.
// The caller doesn't care which one is chosen at runtime — that's the
// whole point of the pattern.
//
// INTERVIEW NOTE: "Why an abstract base class instead of std::variant or
// templates?" Answer: runtime polymorphism. The mode is chosen from CLI
// args at runtime, so we need a v-table. Templates would force compile-
// time selection. std::variant would work but adds visitor boilerplate
// when extending — and we want this to be open for extension.
// (Open/Closed Principle — the "O" in SOLID.)
//
// CROSS-CUTTING CONCERNS:
// Progress reporting and cancellation are NOT part of the core algorithm.
// They are "aspects" injected from the outside via setters. This keeps
// the CLI path (which has no GUI to report to) free of UI plumbing,
// while letting the GUI subscribe to events when it wants them.
// =======================================================================
class IExtractor {
public:
    // virtual destructor: REQUIRED on any class meant for polymorphic
    // deletion via base pointer. Forgetting this is a classic interview
    // gotcha — it leaks the derived class's resources.
    virtual ~IExtractor() = default;

    virtual std::vector<SubtitleEntry> extract(
        const std::filesystem::path& videoPath) = 0;

    virtual std::string name() const = 0;

    // -------- Optional cross-cutting concerns --------

    // Progress callback receives an integer percent in [0, 100].
    // Called from the worker thread; receiver is responsible for
    // marshaling to its own thread if it touches UI.
    using ProgressCallback = std::function<void(int percent)>;
    void setProgressCallback(ProgressCallback cb) { progressCb_ = std::move(cb); }

    // External cancellation flag. Non-owning pointer — owner must
    // outlive the extractor.
    void setCancellationToken(CancellationToken* tok) { cancelTok_ = tok; }

protected:
    // Subclasses call these helpers; they're cheap no-ops when no
    // callback / token was attached.
    void reportProgress(int percent) {
        if (progressCb_) progressCb_(std::clamp(percent, 0, 100));
    }
    CancellationToken* cancellation() const { return cancelTok_; }
    bool isCancelled() const;  // defined in IExtractor.cpp-free header below

    ProgressCallback   progressCb_;
    CancellationToken* cancelTok_ = nullptr;
};

} // namespace subext

// Inline definition of isCancelled() lives in CancellationToken.h.
// Including it here would create a circular-ish include but it's safe
// (CancellationToken doesn't depend on IExtractor).
#include "CancellationToken.h"
namespace subext {
inline bool IExtractor::isCancelled() const {
    return cancelTok_ != nullptr && cancelTok_->isCancelled();
}
}
