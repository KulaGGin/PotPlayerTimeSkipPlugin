#include "core/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <optional>

namespace core {

namespace {

// Same trim/lowercase helpers as core.cpp's own Trim, kept as private
// copies here rather than exported just for this file.
std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string ToLowerAscii(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

std::string_view SeverityName(diagnostics::Severity severity) {
    switch (severity) {
    case diagnostics::Severity::Info:
        return "Info";
    case diagnostics::Severity::Warn:
        return "Warn";
    case diagnostics::Severity::Error:
        return "Error";
    }
    return "Info";
}

std::optional<diagnostics::Severity> ParseSeverityName(std::string_view text) {
    const std::string lower = ToLowerAscii(TrimAscii(text));
    if (lower == "info") return diagnostics::Severity::Info;
    if (lower == "warn" || lower == "warning") return diagnostics::Severity::Warn;
    if (lower == "error") return diagnostics::Severity::Error;
    return std::nullopt;
}

std::optional<bool> ParseBoolValue(std::string_view text) {
    const std::string lower = ToLowerAscii(TrimAscii(text));
    if (lower == "true") return true;
    if (lower == "false") return false;
    return std::nullopt;
}

std::optional<Milliseconds> ParseNonNegativeMs(std::string_view text) {
    text = TrimAscii(text);
    if (text.empty() ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return static_cast<Milliseconds>(value);
}

enum class Section { kNone, kHotkeys, kSkip, kOsd, kLog, kUnknown };

Section SectionFromName(std::string_view name) {
    const std::string lower = ToLowerAscii(name);
    if (lower == "hotkeys") return Section::kHotkeys;
    if (lower == "skip") return Section::kSkip;
    if (lower == "osd") return Section::kOsd;
    if (lower == "log") return Section::kLog;
    return Section::kUnknown;
}

}  // namespace

HotkeySpec ParseHotkeySpec(std::string_view text) {
    text = TrimAscii(text);
    if (text.empty()) {
        throw ParseError("empty hotkey spec");
    }

    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    for (;;) {
        const auto plus = text.find('+', start);
        const bool isLast = plus == std::string_view::npos;
        tokens.push_back(TrimAscii(text.substr(start, isLast ? std::string_view::npos : plus - start)));
        if (isLast) {
            break;
        }
        start = plus + 1;
    }

    if (tokens.back().empty()) {
        throw ParseError("hotkey spec is missing its key: '" + std::string(text) + "'");
    }

    unsigned int modifiers = 0;
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string modifier = ToLowerAscii(tokens[i]);
        unsigned int bit = 0;
        if (modifier == "alt") {
            bit = kModAlt;
        } else if (modifier == "ctrl" || modifier == "control") {
            bit = kModControl;
        } else if (modifier == "shift") {
            bit = kModShift;
        } else if (modifier == "win" || modifier == "meta") {
            bit = kModWin;
        } else {
            throw ParseError("unrecognized modifier '" + std::string(tokens[i]) + "' in '" + std::string(text) +
                              "'");
        }
        if ((modifiers & bit) != 0) {
            throw ParseError("duplicate modifier '" + std::string(tokens[i]) + "' in '" + std::string(text) + "'");
        }
        modifiers |= bit;
    }

    const std::string_view keyToken = tokens.back();
    unsigned int virtualKey = 0;
    if (keyToken.size() == 1) {
        const char c = keyToken[0];
        if (c >= 'a' && c <= 'z') {
            virtualKey = static_cast<unsigned int>(std::toupper(static_cast<unsigned char>(c)));
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            virtualKey = static_cast<unsigned int>(c);
        } else if (c == '[') {
            virtualKey = kVkOpenBracket;
        } else if (c == ']') {
            virtualKey = kVkCloseBracket;
        }
    }
    if (virtualKey == 0) {
        throw ParseError("unrecognized key '" + std::string(keyToken) +
                          "' (expected a single letter A-Z, digit 0-9, '[', or ']'): '" + std::string(text) + "'");
    }

    return HotkeySpec{modifiers, virtualKey};
}

std::string FormatHotkeySpec(const HotkeySpec& spec) {
    std::string text;
    const auto appendModifier = [&](unsigned int bit, const char* name) {
        if ((spec.modifiers & bit) != 0) {
            if (!text.empty()) {
                text += '+';
            }
            text += name;
        }
    };
    appendModifier(kModControl, "Ctrl");
    appendModifier(kModAlt, "Alt");
    appendModifier(kModShift, "Shift");
    appendModifier(kModWin, "Win");

    if (!text.empty()) {
        text += '+';
    }
    if (spec.virtualKey == kVkOpenBracket) {
        text += '[';
    } else if (spec.virtualKey == kVkCloseBracket) {
        text += ']';
    } else if ((spec.virtualKey >= 'A' && spec.virtualKey <= 'Z') ||
               (spec.virtualKey >= '0' && spec.virtualKey <= '9')) {
        text += static_cast<char>(spec.virtualKey);
    } else {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02X", spec.virtualKey);
        text += hex;
    }
    return text;
}

Config DefaultConfig() {
    return Config{};
}

ParsedConfig ParseConfig(std::string_view iniText) {
    ParsedConfig result;
    result.config = DefaultConfig();

    Section section = Section::kNone;

    std::size_t pos = 0;
    while (pos <= iniText.size()) {
        const auto newline = iniText.find('\n', pos);
        std::string_view line =
            (newline == std::string_view::npos) ? iniText.substr(pos) : iniText.substr(pos, newline - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        line = TrimAscii(line);

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            // Blank line or full-line comment: nothing to do.
        } else if (line.front() == '[') {
            if (line.size() < 2 || line.back() != ']') {
                result.warnings.push_back("malformed section header, ignored: '" + std::string(line) + "'");
                section = Section::kUnknown;
            } else {
                section = SectionFromName(line.substr(1, line.size() - 2));
                if (section == Section::kUnknown) {
                    result.warnings.push_back("unknown section '" + std::string(line) + "', its keys are ignored");
                }
            }
        } else {
            const auto eq = line.find('=');
            if (eq == std::string_view::npos) {
                result.warnings.push_back("malformed line, ignored: '" + std::string(line) + "'");
            } else {
                const std::string_view keyView = TrimAscii(line.substr(0, eq));
                const std::string_view valueView = TrimAscii(line.substr(eq + 1));
                const std::string key = ToLowerAscii(keyView);

                if (section == Section::kNone) {
                    result.warnings.push_back("key '" + std::string(keyView) + "' found before any section, ignored");
                } else if (section == Section::kUnknown) {
                    // Already warned about once, when the section header itself was seen.
                } else if (section == Section::kHotkeys) {
                    if (key == "newmark" || key == "markstart" || key == "markend") {
                        try {
                            const HotkeySpec spec = ParseHotkeySpec(valueView);
                            if (key == "newmark") {
                                result.config.newMarkHotkey = spec;
                            } else if (key == "markstart") {
                                result.config.markStartHotkey = spec;
                            } else {
                                result.config.markEndHotkey = spec;
                            }
                        } catch (const ParseError& e) {
                            result.warnings.push_back("[Hotkeys] " + std::string(keyView) + ": " + e.what() +
                                                       ", using default");
                        }
                    } else if (key == "global") {
                        if (const auto value = ParseBoolValue(valueView)) {
                            result.config.globalHotkeys = *value;
                        } else {
                            result.warnings.push_back("[Hotkeys] Global: invalid value '" + std::string(valueView) +
                                                       "', using default");
                        }
                    } else {
                        result.warnings.push_back("unknown key '[Hotkeys] " + std::string(keyView) + "', ignored");
                    }
                } else if (section == Section::kSkip) {
                    if (key == "autoenable") {
                        if (const auto value = ParseBoolValue(valueView)) {
                            result.config.autoEnableSkip = *value;
                        } else {
                            result.warnings.push_back("[Skip] AutoEnable: invalid value '" + std::string(valueView) +
                                                       "', using default");
                        }
                    } else {
                        result.warnings.push_back("unknown key '[Skip] " + std::string(keyView) + "', ignored");
                    }
                } else if (section == Section::kOsd) {
                    if (key == "enabled") {
                        if (const auto value = ParseBoolValue(valueView)) {
                            result.config.osdEnabled = *value;
                        } else {
                            result.warnings.push_back("[Osd] Enabled: invalid value '" + std::string(valueView) +
                                                       "', using default");
                        }
                    } else if (key == "durationms") {
                        if (const auto value = ParseNonNegativeMs(valueView)) {
                            result.config.osdDurationMs = *value;
                        } else {
                            result.warnings.push_back("[Osd] DurationMs: invalid value '" + std::string(valueView) +
                                                       "', using default");
                        }
                    } else {
                        result.warnings.push_back("unknown key '[Osd] " + std::string(keyView) + "', ignored");
                    }
                } else if (section == Section::kLog) {
                    if (key == "verbosity") {
                        if (const auto value = ParseSeverityName(valueView)) {
                            result.config.logVerbosity = *value;
                        } else {
                            result.warnings.push_back("[Log] Verbosity: invalid value '" + std::string(valueView) +
                                                       "', using default");
                        }
                    } else {
                        result.warnings.push_back("unknown key '[Log] " + std::string(keyView) + "', ignored");
                    }
                }
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }

    return result;
}

std::string SerializeConfig(const Config& config) {
    std::string text;
    text += "; PotPlayerTimeSkip plugin configuration.\n";
    text += "; Edit this file, then restart PotPlayer for changes to take effect.\n";
    text += "; Lines starting with ';' are comments and are ignored.\n\n";

    text += "[Hotkeys]\n";
    text += "; Each hotkey is <modifiers>+<key>, e.g. \"Alt+A\" or \"Ctrl+Shift+[\".\n";
    text += "; Modifiers: Alt, Ctrl (or Control), Shift, Win (or Meta). Keys: A-Z, 0-9, [, ].\n";
    text += "NewMark=" + FormatHotkeySpec(config.newMarkHotkey) + "\n";
    text += "MarkStart=" + FormatHotkeySpec(config.markStartHotkey) + "\n";
    text += "MarkEnd=" + FormatHotkeySpec(config.markEndHotkey) + "\n";
    text += "; Global=true makes the hotkeys work even when PotPlayer isn't the\n";
    text += "; foreground window. Default (false) only reacts while you're watching.\n";
    text += std::string("Global=") + (config.globalHotkeys ? "true" : "false") + "\n\n";

    text += "[Skip]\n";
    text += "; AutoEnable=true automatically turns on Skip Setup's \"Enable skip\n";
    text += "; feature\" checkbox the first time a range is saved, if it was off.\n";
    text += std::string("AutoEnable=") + (config.autoEnableSkip ? "true" : "false") + "\n\n";

    text += "[Osd]\n";
    text += "; On-screen feedback for the hotkeys above.\n";
    text += std::string("Enabled=") + (config.osdEnabled ? "true" : "false") + "\n";
    text += "DurationMs=" + std::to_string(config.osdDurationMs) + "\n\n";

    text += "[Log]\n";
    text += "; One of Info, Warn, Error. Higher means less noisy.\n";
    text += "Verbosity=" + std::string(SeverityName(config.logVerbosity)) + "\n";

    return text;
}

}  // namespace core
