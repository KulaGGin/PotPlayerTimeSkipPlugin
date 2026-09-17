#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/core.hpp"
#include "diagnostics/format.hpp"

// PTS-016: the config file's pure parsing/validation half — human-editable
// INI text <-> a fully-resolved core::Config, no file I/O, no <windows.h>,
// unit-testable the same way as the rest of core/ (see core/core.hpp's own
// header comment). The live half — locating and creating
// %LOCALAPPDATA%\PotPlayerTimeSkip\config.ini and logging every warning this
// file reports — is plugin::LoadConfig (plugin/config.hpp).
namespace core {

// winuser.h's own RegisterHotKey modifier bits, spelled numerically so this
// header stays <windows.h>-free — the same convention plugin/hotkeys.hpp
// already uses for kModAlt/kVkA (PTS-013). Kept here too (rather than
// reused from there) since plugin/ depends on core/, never the other way.
inline constexpr unsigned int kModAlt = 0x0001;
inline constexpr unsigned int kModControl = 0x0002;
inline constexpr unsigned int kModShift = 0x0004;
inline constexpr unsigned int kModWin = 0x0008;

// VK_OEM_4 / VK_OEM_6 on the US layout this project targets — PTS-013's
// Alt+[ / Alt+] defaults. Letters and digits need no named constant: their
// VK_* codes equal their own uppercase ASCII value.
inline constexpr unsigned int kVkOpenBracket = 0xDB;
inline constexpr unsigned int kVkCloseBracket = 0xDD;

// A RegisterHotKey-shaped modifier/key pair: `modifiers` is a bitmask of
// the kMod* constants above, `virtualKey` a VK_* code.
struct HotkeySpec {
    unsigned int modifiers = 0;
    unsigned int virtualKey = 0;

    bool operator==(const HotkeySpec&) const = default;
};

// Parses one hotkey spec like "Alt+A", "Ctrl+Shift+[", or "Alt+]":
// '+'-separated, case-insensitive modifier names (Alt, Ctrl/Control, Shift,
// Win/Meta) in any order, followed by exactly one key — a single letter
// (A-Z), digit (0-9), or '[' / ']' (the two OEM keys PTS-013's defaults
// use). Throws ParseError on anything else: a missing key, an unrecognized
// modifier or key, or a modifier repeated twice — the same "never coerce,
// always throw" contract as the rest of core/.
HotkeySpec ParseHotkeySpec(std::string_view text);

// The exact inverse of ParseHotkeySpec, e.g. HotkeySpec{kModAlt, kVkA} ->
// "Alt+A" — modifiers always rendered in Ctrl/Alt/Shift/Win order regardless
// of the order ParseHotkeySpec was given them.
std::string FormatHotkeySpec(const HotkeySpec& spec);

// Everything PTS-016's config file exposes, always fully resolved — every
// field either came from a valid line in the file or its own default,
// never a "not yet loaded" placeholder. This is what the plugin consumes;
// the fallback-to-default behavior for a missing/garbled file lives
// entirely in ParseConfig below (and, past that, plugin::LoadConfig for
// I/O failures), so this struct itself never needs an "is this valid"
// check.
//
// Commit strategy (skip_marking.hpp's "commit-on-complete") isn't
// configurable here: PTS-014 only ever implemented that one strategy, so
// there is nothing to choose between yet.
struct Config {
    HotkeySpec newMarkHotkey{kModAlt, 'A'};              // Alt+A (PTS-013)
    HotkeySpec markStartHotkey{kModAlt, kVkOpenBracket};  // Alt+[ (PTS-013)
    HotkeySpec markEndHotkey{kModAlt, kVkCloseBracket};   // Alt+] (PTS-013)
    bool globalHotkeys = false;                           // false = foreground-only (PTS-013's default)
    bool autoEnableSkip = true;                           // PTS-012's default
    bool osdEnabled = true;                                // PTS-015's default
    Milliseconds osdDurationMs = 1800;                     // PTS-015's kDismissMs
    diagnostics::Severity logVerbosity = diagnostics::Severity::Info;  // PTS-004's default
};

// Config with every field at its default — what a missing config.ini is
// created from, and what any individual malformed key in ParseConfig below
// falls back to.
Config DefaultConfig();

// A parsed config plus one human-readable line per fallback ParseConfig had
// to take: a malformed hotkey spec, a non-boolean/non-numeric value, an
// unknown key or section, a line outside any section. plugin::LoadConfig
// logs each of these via LOG_WARN; core itself never logs (kept
// side-effect-free like the rest of this library).
struct ParsedConfig {
    Config config;
    std::vector<std::string> warnings;
};

// Parses a config.ini's full text (see README.md's Configuration section
// for the recognized sections/keys). A blank or fully-absent file parses to
// DefaultConfig() with no warnings. Per PTS-016's design notes: an unknown
// section or key is ignored with a warning, never an error; a key present
// but malformed falls back to that key's own default, also with a warning.
// Nothing in this function throws, so a garbled config.ini can never stop
// the plugin from loading.
ParsedConfig ParseConfig(std::string_view iniText);

// The inverse of ParseConfig: renders `config` as a commented, human-editable
// config.ini. Used to write the default file on first run.
// SerializeConfig(c) parsed back through ParseConfig always reproduces `c`
// with zero warnings (tests/unit/core/config_test.cpp checks this for
// DefaultConfig()).
std::string SerializeConfig(const Config& config);

}
