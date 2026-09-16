#include "diagnostics/format.hpp"

#include <cstdio>

namespace diagnostics {

std::string_view SeverityTag(Severity severity) {
    switch (severity) {
    case Severity::Info:
        return "INFO ";
    case Severity::Warn:
        return "WARN ";
    case Severity::Error:
        return "ERROR";
    }
    return "?????";
}

std::string FormatLine(Severity severity, const Timestamp& timestamp, std::uint32_t threadId,
                       std::string_view message) {
    char header[64];
    const int written = std::snprintf(
        header, sizeof(header), "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [tid %u] [%.*s] ",
        timestamp.year, timestamp.month, timestamp.day, timestamp.hour, timestamp.minute,
        timestamp.second, timestamp.millisecond, static_cast<unsigned>(threadId),
        static_cast<int>(SeverityTag(severity).size()), SeverityTag(severity).data());

    std::string line;
    line.reserve((written > 0 ? static_cast<std::size_t>(written) : 0) + message.size() + 1);
    if (written > 0) {
        line.append(header, static_cast<std::size_t>(written));
    }
    line.append(message);
    line.push_back('\n');
    return line;
}

}
