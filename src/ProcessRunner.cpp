#include "ProcessRunner.h"
#include "CancellationToken.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace subext {

namespace {

// Quote an argv element so the shell / CreateProcess parser sees it
// as a single token. Cheap but correct enough for our pre-validated
// args (no embedded quotes in real usage).
std::string quoteArg(const std::string& a) {
    if (a.find_first_of(" \t\"\\") == std::string::npos) return a;
    std::string r = "\"";
    for (char c : a) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    r += '"';
    return r;
}

std::string buildCommandLine(const std::string& exe,
                              const std::vector<std::string>& args) {
    std::ostringstream cmd;
    cmd << quoteArg(exe);
    for (const auto& a : args) cmd << ' ' << quoteArg(a);
    return cmd.str();
}

// Split a chunk of bytes into lines by '\n' OR '\r'. FFmpeg uses '\r'
// for in-place progress updates, so we must treat it as a terminator.
// Emits each completed line via onLine; partial trailing line stays
// in `buf` for the next chunk.
template <typename LineCB>
void feedChunk(const char* data, size_t n, std::string& buf, LineCB&& onLine) {
    for (size_t i = 0; i < n; ++i) {
        char c = data[i];
        if (c == '\n' || c == '\r') {
            if (!buf.empty()) {
                onLine(std::string_view(buf));
                buf.clear();
            }
        } else {
            buf += c;
        }
    }
}

} // anonymous namespace


#ifdef _WIN32

// ============================================================
// WINDOWS streaming impl: CreateProcess + anonymous pipe.
// Real cancellation via TerminateProcess from a watchdog thread.
// ============================================================
ProcessResult ProcessRunner::runStreaming(const StreamingProcessOptions& opts) {
    ProcessResult result;

    // SECURITY_ATTRIBUTES with bInheritHandle=TRUE so the child can
    // inherit the write end of our pipe.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }
    // The parent (us) reads — make sure that handle is NOT inherited
    // by the child. Otherwise child holds a copy and the pipe never
    // closes when child exits, leaving us blocked in ReadFile forever.
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    std::string cmd = buildCommandLine(opts.executable, opts.args);
    // CreateProcessA wants a writable buffer for lpCommandLine.
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr,                  // application name (parsed from cmd)
        cmdBuf.data(),
        nullptr, nullptr,         // process / thread security
        TRUE,                     // bInheritHandles
        CREATE_NO_WINDOW,         // hide subprocess console
        nullptr, nullptr,         // env, cwd
        &si, &pi);

    // Close the parent's copy of the write end immediately.
    // When the child exits, the last writer is gone, ReadFile returns 0.
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        DWORD err = GetLastError();
        throw std::runtime_error(
            "CreateProcess failed (code " + std::to_string(err) +
            ") for: " + opts.executable);
    }

    // Watchdog thread: polls the cancel token. When set, calls
    // TerminateProcess so the child dies and our ReadFile unblocks.
    HANDLE hStopWatchdog = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    std::thread watchdog;
    if (opts.cancel) {
        watchdog = std::thread([hStopWatchdog, hProc = pi.hProcess, cancel = opts.cancel]() {
            while (WaitForSingleObject(hStopWatchdog, 100) == WAIT_TIMEOUT) {
                if (cancel->isCancelled()) {
                    TerminateProcess(hProc, 1);
                    return;
                }
            }
        });
    }

    // Read loop on the parent side. Blocks until child closes the
    // pipe (i.e. exits or is terminated by the watchdog).
    std::string lineBuf;
    std::array<char, 4096> chunk{};
    for (;;) {
        DWORD nRead = 0;
        BOOL  rok   = ReadFile(hRead, chunk.data(),
                                static_cast<DWORD>(chunk.size()),
                                &nRead, nullptr);
        if (!rok || nRead == 0) break;
        feedChunk(chunk.data(), nRead, lineBuf, [&](std::string_view sv) {
            if (opts.onLine) opts.onLine(sv);
        });
    }
    if (!lineBuf.empty() && opts.onLine) {
        opts.onLine(std::string_view(lineBuf));
    }

    // Wait for the child and reap its exit code.
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    // Stop the watchdog thread cleanly.
    SetEvent(hStopWatchdog);
    if (watchdog.joinable()) watchdog.join();
    CloseHandle(hStopWatchdog);

    if (opts.cancel && opts.cancel->isCancelled()) {
        result.cancelled = true;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);
    return result;
}

#else // POSIX

// ============================================================
// POSIX streaming impl: simple popen-based, polls cancel
// between lines (no real subprocess kill — TODO).
// ============================================================
ProcessResult ProcessRunner::runStreaming(const StreamingProcessOptions& opts) {
    ProcessResult result;

    std::string cmd = buildCommandLine(opts.executable, opts.args) + " 2>&1";
    FILE* raw = popen(cmd.c_str(), "r");
    if (!raw) throw std::runtime_error("popen failed for: " + opts.executable);

    std::string lineBuf;
    std::array<char, 4096> chunk{};
    while (size_t n = std::fread(chunk.data(), 1, chunk.size(), raw)) {
        feedChunk(chunk.data(), n, lineBuf, [&](std::string_view sv) {
            if (opts.onLine) opts.onLine(sv);
        });
        if (opts.cancel && opts.cancel->isCancelled()) {
            result.cancelled = true;
            break;
        }
    }
    if (!lineBuf.empty() && opts.onLine) opts.onLine(std::string_view(lineBuf));

    result.exitCode = pclose(raw);
    return result;
}

#endif

// Old blocking API: implemented on top of streaming, with a
// buffering callback that just appends each line to result.output.
ProcessResult ProcessRunner::run(const std::string& executable,
                                  const std::vector<std::string>& args) {
    StreamingProcessOptions opts;
    opts.executable = executable;
    opts.args       = args;
    ProcessResult buffered;
    opts.onLine = [&buffered](std::string_view line) {
        buffered.output.append(line.data(), line.size());
        buffered.output += '\n';
    };
    auto r = runStreaming(opts);
    buffered.exitCode  = r.exitCode;
    buffered.cancelled = r.cancelled;
    return buffered;
}

} // namespace subext
