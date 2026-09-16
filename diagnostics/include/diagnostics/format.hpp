#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Pure line-formatting logic for the diagnostic log. Kept free of
// <windows.h> so it stays unit-testable on its own, mirroring core/'s
// separation of platform-free logic from the live player (see
// core/include/core/core.hpp). The Win32-facing sink lives in log.hpp.
namespace diagnostics {

enum class Severity { Info, Warn, Error };

// Human-readable, fixed-width tag used in formatted lines, e.g. "INFO ".
std::string_view SeverityTag(Severity severity);

struct Timestamp {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
};

// Renders one greppable, newline-terminated log line:
// "[YYYY-MM-DD HH:MM:SS.mmm] [tid 1234] [INFO ] message\n"
// `message` is passed through byte-for-byte (expected to already be UTF-8),
// never re-encoded, so non-ASCII content round-trips without corruption.
std::string FormatLine(Severity severity, const Timestamp& timestamp, std::uint32_t threadId,
                       std::string_view message);

}
