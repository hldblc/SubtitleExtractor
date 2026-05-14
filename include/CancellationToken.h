#pragma once
#include <atomic>

namespace subext {

// =======================================================================
// CANCELLATION TOKEN
// =======================================================================
// Cooperative cancellation primitive. One thread (typically the GUI)
// calls cancel(); another thread (the worker) polls isCancelled() at
// safe points and bails out cleanly.
//
// This is the C++ analogue of:
//   - .NET's System.Threading.CancellationToken
//   - Java's Thread.interrupt() flag
//   - Go's <-ctx.Done() channel
//
// We do NOT use std::thread::join + force-kill, because preemptively
// killing a thread leaves locks held, destructors unrun, and memory
// in undefined state. Cooperative cancellation lets the worker reach
// a known-safe state before unwinding.
//
// INTERVIEW NOTE: "How do you stop a long-running thread safely?"
// Wrong answer: TerminateThread / pthread_kill.
// Right answer: cooperative cancellation via a shared atomic flag,
// combined with killing any blocked syscalls (e.g. TerminateProcess
// on a subprocess) so the read loop can observe the flag.
// =======================================================================
class CancellationToken {
public:
    void cancel() noexcept       { cancelled_.store(true, std::memory_order_release); }
    bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    void reset() noexcept        { cancelled_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> cancelled_{false};
};

} // namespace subext
