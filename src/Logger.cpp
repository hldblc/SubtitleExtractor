#include "Logger.h"
#include <iostream>

namespace subext::log {

// Anonymous namespace == "static" linkage in a translation unit.
// Anything declared here is invisible outside this .cpp file.
namespace {
    bool gVerbose = false;
}

void set_verbose(bool v) { gVerbose = v; }
bool verbose()           { return gVerbose; }

void info (std::string_view m) { std::cout << "[INFO]  " << m << '\n'; }
void warn (std::string_view m) { std::cerr << "[WARN]  " << m << '\n'; }
void error(std::string_view m) { std::cerr << "[ERROR] " << m << '\n'; }
void debug(std::string_view m) {
    if (gVerbose) std::cerr << "[DEBUG] " << m << '\n';
}

} // namespace subext::log
