#include "core/core.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace core {

int add(int lhs, int rhs) {
    return lhs + rhs;
}

namespace {

bool IsAllDigits(std::string_view text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Parses an all-digit field into an integral value, throwing ParseError
// (naming `fieldName` and the offending text) on anything else: empty,
// containing a non-digit, or too large to fit.
long long ParseDigitsField(std::string_view text, std::string_view fieldName) {
    if (!IsAllDigits(text)) {
        throw ParseError("invalid " + std::string(fieldName) + ": '" + std::string(text) + "'");
    }
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc()) {
        throw ParseError("invalid " + std::string(fieldName) + ": '" + std::string(text) + "'");
    }
    return value;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string FormatTimecode(Milliseconds ms) {
    if (ms < 0) {
        throw ParseError("cannot format a negative duration as a timecode");
    }

    const Milliseconds totalSeconds = ms / 1000;
    const int millis = static_cast<int>(ms % 1000);
    const long long hours = static_cast<long long>(totalSeconds / 3600);
    const int minutes = static_cast<int>((totalSeconds % 3600) / 60);
    const int seconds = static_cast<int>(totalSeconds % 60);

    char buffer[48];
    const int written = std::snprintf(buffer, sizeof(buffer), "%02lld:%02d:%02d.%03d", hours, minutes,
                                       seconds, millis);
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::string FormatTimecodeShort(Milliseconds ms) {
    const std::string full = FormatTimecode(ms);
    return full.substr(0, full.find('.'));
}

Milliseconds ParseTimecode(std::string_view text) {
    const auto firstColon = text.find(':');
    if (firstColon == std::string_view::npos) {
        throw ParseError("invalid timecode (expected HH:MM:SS.mmm): '" + std::string(text) + "'");
    }
    const auto secondColon = text.find(':', firstColon + 1);
    if (secondColon == std::string_view::npos) {
        throw ParseError("invalid timecode (expected HH:MM:SS.mmm): '" + std::string(text) + "'");
    }
    const auto dot = text.find('.', secondColon + 1);
    if (dot == std::string_view::npos) {
        throw ParseError("invalid timecode (expected HH:MM:SS.mmm): '" + std::string(text) + "'");
    }

    const std::string_view hoursText = text.substr(0, firstColon);
    const std::string_view minutesText = text.substr(firstColon + 1, secondColon - firstColon - 1);
    const std::string_view secondsText = text.substr(secondColon + 1, dot - secondColon - 1);
    const std::string_view millisText = text.substr(dot + 1);

    if (minutesText.size() != 2 || secondsText.size() != 2 || millisText.size() != 3) {
        throw ParseError("invalid timecode (expected HH:MM:SS.mmm): '" + std::string(text) + "'");
    }

    const long long hours = ParseDigitsField(hoursText, "timecode hours");
    const long long minutes = ParseDigitsField(minutesText, "timecode minutes");
    const long long seconds = ParseDigitsField(secondsText, "timecode seconds");
    const long long millis = ParseDigitsField(millisText, "timecode milliseconds");

    if (minutes > 59) {
        throw ParseError("invalid timecode minutes (expected 00-59): '" + std::string(minutesText) + "'");
    }
    if (seconds > 59) {
        throw ParseError("invalid timecode seconds (expected 00-59): '" + std::string(secondsText) + "'");
    }

    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
}

SkipRange SkipRange::Create(Milliseconds startMs, Milliseconds endMs) {
    if (startMs < 0) {
        throw ParseError("skip range start must not be negative");
    }
    if (endMs <= startMs) {
        throw ParseError("skip range end must be strictly after start (backwards or zero-length range)");
    }
    return SkipRange(startMs, endMs);
}

SkipRange::SkipRange(Milliseconds startMs, Milliseconds endMs) : startMs_(startMs), endMs_(endMs) {}

std::string SerializePbfLine(const PbfEntry& entry) {
    return std::to_string(entry.index) + "=" + std::to_string(entry.type) + "*" +
           std::to_string(entry.range.StartMs()) + "*" + std::to_string(entry.range.LengthMs());
}

PbfEntry ParsePbfLine(std::string_view line) {
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) {
        throw ParseError("invalid .pbf line (missing '='): '" + std::string(line) + "'");
    }

    const std::string_view indexText = line.substr(0, eq);
    const std::string_view rest = line.substr(eq + 1);
    if (rest.empty()) {
        throw ParseError("'" + std::string(line) + "' is the .pbf section terminator, not an entry");
    }

    const auto star1 = rest.find('*');
    if (star1 == std::string_view::npos) {
        throw ParseError("invalid .pbf line (expected type*start_ms*length_ms): '" + std::string(line) + "'");
    }
    const auto star2 = rest.find('*', star1 + 1);
    if (star2 == std::string_view::npos) {
        throw ParseError("invalid .pbf line (expected type*start_ms*length_ms): '" + std::string(line) + "'");
    }

    const long long index = ParseDigitsField(indexText, "pbf index");
    const long long type = ParseDigitsField(rest.substr(0, star1), "pbf type");
    const long long startMs = ParseDigitsField(rest.substr(star1 + 1, star2 - star1 - 1), "pbf start_ms");
    const long long lengthMs = ParseDigitsField(rest.substr(star2 + 1), "pbf length_ms");

    return PbfEntry{static_cast<int>(index), static_cast<int>(type),
                     SkipRange::Create(startMs, startMs + lengthMs)};
}

std::string BuildPlaySkipSection(const std::vector<SkipRange>& ranges, int type) {
    std::string section = "[PlaySkip]\r\n";
    int index = 0;
    for (const auto& range : ranges) {
        section += SerializePbfLine(PbfEntry{index, type, range});
        section += "\r\n";
        ++index;
    }
    section += std::to_string(index) + "=\r\n";
    return section;
}

std::vector<SkipRange> ParsePlaySkipSection(std::string_view section) {
    std::vector<SkipRange> ranges;

    std::size_t pos = 0;
    while (pos <= section.size()) {
        const auto newline = section.find('\n', pos);
        std::string_view line =
            (newline == std::string_view::npos) ? section.substr(pos) : section.substr(pos, newline - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!line.empty() && line != "[PlaySkip]") {
            const auto eq = line.find('=');
            const bool isTerminator = eq != std::string_view::npos && line.substr(eq + 1).empty();
            if (!isTerminator) {
                ranges.push_back(ParsePbfLine(line).range);
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }

    return ranges;
}

Milliseconds ParseHumanTimecode(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        throw ParseError("empty timecode");
    }

    std::string_view integerPart = text;
    long long millis = 0;

    const auto dot = text.find('.');
    if (dot != std::string_view::npos) {
        integerPart = text.substr(0, dot);
        const std::string_view fraction = text.substr(dot + 1);
        if (fraction.empty() || fraction.size() > 3 || !IsAllDigits(fraction)) {
            throw ParseError("invalid fractional seconds in timecode: '" + std::string(text) + "'");
        }
        millis = ParseDigitsField(fraction, "fractional seconds");
        for (std::size_t i = fraction.size(); i < 3; ++i) {
            millis *= 10;
        }
    }

    if (integerPart.empty()) {
        throw ParseError("invalid timecode: '" + std::string(text) + "'");
    }

    std::array<std::string_view, 3> fields{};
    std::size_t fieldCount = 0;
    std::size_t start = 0;
    for (;;) {
        if (fieldCount == fields.size()) {
            throw ParseError("invalid timecode (too many ':'-separated fields): '" + std::string(text) + "'");
        }
        const auto colon = integerPart.find(':', start);
        const bool isLast = colon == std::string_view::npos;
        fields[fieldCount++] = integerPart.substr(start, isLast ? std::string_view::npos : colon - start);
        if (isLast) {
            break;
        }
        start = colon + 1;
    }

    long long hours = 0;
    long long minutes = 0;
    long long seconds;

    if (fieldCount == 1) {
        seconds = ParseDigitsField(fields[0], "seconds");
    } else if (fieldCount == 2) {
        minutes = ParseDigitsField(fields[0], "minutes");
        if (minutes > 59) {
            throw ParseError("invalid timecode minutes (expected 0-59): '" + std::string(fields[0]) + "'");
        }
        seconds = ParseDigitsField(fields[1], "seconds");
        if (seconds > 59) {
            throw ParseError("invalid timecode seconds (expected 0-59): '" + std::string(fields[1]) + "'");
        }
    } else {
        hours = ParseDigitsField(fields[0], "hours");
        minutes = ParseDigitsField(fields[1], "minutes");
        if (minutes > 59) {
            throw ParseError("invalid timecode minutes (expected 0-59): '" + std::string(fields[1]) + "'");
        }
        seconds = ParseDigitsField(fields[2], "seconds");
        if (seconds > 59) {
            throw ParseError("invalid timecode seconds (expected 0-59): '" + std::string(fields[2]) + "'");
        }
    }

    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
}

SkipRange ParseHumanRange(std::string_view text) {
    text = Trim(text);
    if (text.empty()) {
        throw ParseError("empty range");
    }

    // Checked in this order — not alphabetical — so "->" is matched whole
    // before its leading '-' could be mistaken for the plain "-" separator.
    static constexpr std::array<std::string_view, 3> kSeparators{"->", "~", "-"};

    for (const std::string_view separator : kSeparators) {
        const auto pos = text.find(separator);
        if (pos == std::string_view::npos) {
            continue;
        }

        const std::string_view left = Trim(text.substr(0, pos));
        const std::string_view right = Trim(text.substr(pos + separator.size()));
        if (left.empty() || right.empty()) {
            throw ParseError("invalid range (missing start or end): '" + std::string(text) + "'");
        }

        const Milliseconds startMs = ParseHumanTimecode(left);
        const Milliseconds endMs = ParseHumanTimecode(right);
        return SkipRange::Create(startMs, endMs);
    }

    throw ParseError("invalid range (expected a '-', '~', or '->' separator): '" + std::string(text) + "'");
}

}  // namespace core
