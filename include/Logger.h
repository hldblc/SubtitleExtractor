#pragma once
#include <string_view>

// Minimal logging facility. In a real production app you would reach
// for spdlog or a similar library, but writing your own teaches you a
// lot. NASA / defense codebases often use custom logging because of
// audit / determinism requirements.
namespace subext::log {

void set_verbose(bool v);
bool verbose();

void info (std::string_view msg);
void warn (std::string_view msg);
void error(std::string_view msg);
void debug(std::string_view msg);   // only printed when verbose() is true

} // namespace subext::log
