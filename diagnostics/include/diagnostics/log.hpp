#pragma once

#include <atomic>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "diagnostics/format.hpp"

// The plugin's always-available diagnostic log (PTS-004). There is no
// console inside PotPlayer's process and a crash in DllMain or a worker
// thread otherwise leaves nothing to look at, so this is the primary
// debugging instrument for everything from PTS-006 onward.
//
// Writes UTF-8 bytes to
//   %LOCALAPPDATA%\PotPlayerTimeSkip\plugin.log
// (created on first write) and mirrors every line to OutputDebugString for
// a live DebugView/debugger. Safe to call from a DllMain-like context: the
// sink talks to kernel32 only (CreateFileW/WriteFile/OutputDebugStringW),
// never loads another module, and never spins up or waits on another
// thread, so it never contends the loader lock.
//
// Usable from any translation unit in proxy/plugin via the LOG_* macros
// below. `fmt`/`args` follow std::format's rules (compile-time checked).
namespace diagnostics {

// Below this severity, LOG_* is a no-op that never formats its arguments.
// Raising this at runtime (or overriding DIAGNOSTICS_DEFAULT_MIN_SEVERITY
// at compile time) is the verbosity switch called out in PTS-004; wiring
// it to user-facing config is PTS-016's job.
#ifndef DIAGNOSTICS_DEFAULT_MIN_SEVERITY
#define DIAGNOSTICS_DEFAULT_MIN_SEVERITY ::diagnostics::Severity::Info
#endif

void SetMinSeverity(Severity severity);
Severity GetMinSeverity();
bool IsEnabled(Severity severity);

// Writes one already-formatted, UTF-8 message. Prefer LOG_INFO/WARN/ERROR
// over calling this directly.
void LogRaw(Severity severity, std::string_view utf8Message);

template <typename... Args>
void LogFormat(Severity severity, std::format_string<Args...> fmt, Args&&... args) {
    LogRaw(severity, std::format(fmt, std::forward<Args>(args)...));
}

}

#define LOG_INFO(...)                                                          \
    do {                                                                       \
        if (::diagnostics::IsEnabled(::diagnostics::Severity::Info)) {         \
            ::diagnostics::LogFormat(::diagnostics::Severity::Info, __VA_ARGS__); \
        }                                                                      \
    } while (0)

#define LOG_WARN(...)                                                          \
    do {                                                                       \
        if (::diagnostics::IsEnabled(::diagnostics::Severity::Warn)) {         \
            ::diagnostics::LogFormat(::diagnostics::Severity::Warn, __VA_ARGS__); \
        }                                                                      \
    } while (0)

#define LOG_ERROR(...)                                                         \
    do {                                                                       \
        if (::diagnostics::IsEnabled(::diagnostics::Severity::Error)) {        \
            ::diagnostics::LogFormat(::diagnostics::Severity::Error, __VA_ARGS__); \
        }                                                                      \
    } while (0)
