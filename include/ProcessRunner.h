#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace subext {

class CancellationToken;

// Result of running an external process.
struct ProcessResult {
    int         exitCode{-1};
    std::string output;     // combined stdout+stderr (run() only)
    bool        cancelled{false};
};

// Options for the streaming variant. Each line of merged stdout+stderr
// is passed to onLine as it arrives, so callers can parse progress
// in real time and update the GUI.
//
// DESIGN NOTE: this is the OBSERVER PATTERN. The process is the subject;
// the onLine callback is the observer. Compared to "buffer everything,
// return at end" (the run() flavor), it trades a small complexity cost
// for the ability to react to in-flight data — exactly what a progress
// bar needs.
struct StreamingProcessOptions {
    std::string                                  executable;
    std::vector<std::string>                     args;
    std::function<void(std::string_view line)>   onLine;
    CancellationToken*                           cancel{nullptr};
};

class ProcessRunner {
public:
    // Original blocking API: collects all output into result.output.
    // Internally delegates to runStreaming with a buffering callback.
    static ProcessResult run(const std::string& executable,
                              const std::vector<std::string>& args);

    // Streaming variant. Returns when the subprocess exits OR when the
    // cancel token is set (in which case the subprocess is terminated
    // and result.cancelled == true).
    static ProcessResult runStreaming(const StreamingProcessOptions& opts);

    // Set a directory that gets checked FIRST when launching a tool.
    // Used by the GUI to prefer bundled ffmpeg.exe next to the app
    // over whatever PATH happens to provide.
    //
    // The library itself is Qt-free; the caller (gui_main.cpp) supplies
    // the directory via QCoreApplication::applicationDirPath(). Pass an
    // empty string to disable the override.
    static void setBundledBinaryDir(const std::string& dir);
};

} // namespace subext
