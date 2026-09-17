#include <catch_amalgamated.hpp>

#include <string>

#include "core/config.hpp"

using core::Config;
using core::DefaultConfig;
using core::FormatHotkeySpec;
using core::HotkeySpec;
using core::ParseConfig;
using core::ParseError;
using core::ParseHotkeySpec;
using core::SerializeConfig;

TEST_CASE("ParseHotkeySpec accepts modifiers in any order plus a letter, digit, or bracket key",
          "[core][config]") {
    struct Case {
        const char* text;
        unsigned int modifiers;
        unsigned int virtualKey;
    };

    const auto testCase = GENERATE(
        Case{"Alt+A", core::kModAlt, 'A'},
        Case{"alt+a", core::kModAlt, 'A'},
        Case{"Alt+[", core::kModAlt, core::kVkOpenBracket},
        Case{"Alt+]", core::kModAlt, core::kVkCloseBracket},
        Case{"Ctrl+Shift+9", core::kModControl | core::kModShift, '9'},
        Case{"Shift+Ctrl+9", core::kModControl | core::kModShift, '9'},
        Case{"Control+A", core::kModControl, 'A'},
        Case{"Win+Z", core::kModWin, 'Z'},
        Case{"Meta+Z", core::kModWin, 'Z'},
        Case{" Alt + A ", core::kModAlt, 'A'},
        Case{"Z", 0u, 'Z'}
    );

    CAPTURE(testCase.text);
    const HotkeySpec spec = ParseHotkeySpec(testCase.text);
    REQUIRE(spec.modifiers == testCase.modifiers);
    REQUIRE(spec.virtualKey == testCase.virtualKey);
}

TEST_CASE("ParseHotkeySpec rejects a missing key, an unrecognized token, or a duplicate modifier",
          "[core][config]") {
    const std::string text = GENERATE(std::string(""), std::string("   "), std::string("Alt+"),
                                       std::string("Alt+F1"),          // unsupported key
                                       std::string("Bogus+A"),          // unrecognized modifier
                                       std::string("Alt+Alt+A"),        // duplicate modifier
                                       std::string("Alt+AB"));          // multi-character key

    CAPTURE(text);
    REQUIRE_THROWS_AS(ParseHotkeySpec(text), ParseError);
}

TEST_CASE("FormatHotkeySpec renders modifiers in Ctrl/Alt/Shift/Win order", "[core][config]") {
    REQUIRE(FormatHotkeySpec(HotkeySpec{core::kModAlt, 'A'}) == "Alt+A");
    REQUIRE(FormatHotkeySpec(HotkeySpec{core::kModAlt, core::kVkOpenBracket}) == "Alt+[");
    REQUIRE(FormatHotkeySpec(HotkeySpec{core::kModControl | core::kModAlt | core::kModShift | core::kModWin, '9'}) ==
            "Ctrl+Alt+Shift+Win+9");
    REQUIRE(FormatHotkeySpec(HotkeySpec{0, 'Z'}) == "Z");
}

TEST_CASE("FormatHotkeySpec and ParseHotkeySpec round-trip", "[core][config]") {
    const HotkeySpec spec =
        GENERATE(HotkeySpec{core::kModAlt, 'A'}, HotkeySpec{core::kModAlt, core::kVkOpenBracket},
                 HotkeySpec{core::kModControl | core::kModShift, '9'}, HotkeySpec{0, 'Z'});

    REQUIRE(ParseHotkeySpec(FormatHotkeySpec(spec)) == spec);
}

TEST_CASE("DefaultConfig matches PTS-013/012/015/004's existing defaults", "[core][config]") {
    const Config config = DefaultConfig();
    REQUIRE(config.newMarkHotkey == HotkeySpec{core::kModAlt, 'A'});
    REQUIRE(config.markStartHotkey == HotkeySpec{core::kModAlt, core::kVkOpenBracket});
    REQUIRE(config.markEndHotkey == HotkeySpec{core::kModAlt, core::kVkCloseBracket});
    REQUIRE_FALSE(config.globalHotkeys);
    REQUIRE(config.autoEnableSkip);
    REQUIRE(config.osdEnabled);
    REQUIRE(config.osdDurationMs == 1800);
    REQUIRE(config.logVerbosity == diagnostics::Severity::Info);
}

TEST_CASE("ParseConfig on empty text returns defaults with no warnings", "[core][config]") {
    const auto parsed = ParseConfig("");
    REQUIRE(parsed.config.newMarkHotkey == DefaultConfig().newMarkHotkey);
    REQUIRE(parsed.warnings.empty());
}

TEST_CASE("ParseConfig applies every recognized key with no warnings", "[core][config]") {
    const std::string ini =
        "[Hotkeys]\n"
        "NewMark=Ctrl+A\n"
        "MarkStart=Ctrl+[\n"
        "MarkEnd=Ctrl+]\n"
        "Global=true\n"
        "\n"
        "[Skip]\n"
        "AutoEnable=false\n"
        "\n"
        "[Osd]\n"
        "Enabled=false\n"
        "DurationMs=500\n"
        "\n"
        "[Log]\n"
        "Verbosity=Error\n";

    const auto parsed = ParseConfig(ini);
    REQUIRE(parsed.warnings.empty());
    REQUIRE(parsed.config.newMarkHotkey == HotkeySpec{core::kModControl, 'A'});
    REQUIRE(parsed.config.markStartHotkey == HotkeySpec{core::kModControl, core::kVkOpenBracket});
    REQUIRE(parsed.config.markEndHotkey == HotkeySpec{core::kModControl, core::kVkCloseBracket});
    REQUIRE(parsed.config.globalHotkeys);
    REQUIRE_FALSE(parsed.config.autoEnableSkip);
    REQUIRE_FALSE(parsed.config.osdEnabled);
    REQUIRE(parsed.config.osdDurationMs == 500);
    REQUIRE(parsed.config.logVerbosity == diagnostics::Severity::Error);
}

TEST_CASE("ParseConfig ignores comments and blank lines", "[core][config]") {
    const std::string ini =
        "; a comment\n"
        "\n"
        "# also a comment\n"
        "[Log]\n"
        "; another comment\n"
        "Verbosity=Warn\n";

    const auto parsed = ParseConfig(ini);
    REQUIRE(parsed.warnings.empty());
    REQUIRE(parsed.config.logVerbosity == diagnostics::Severity::Warn);
}

TEST_CASE("ParseConfig falls back to the default and warns on a malformed value, never throwing",
          "[core][config]") {
    struct Case {
        const char* ini;
    };

    const auto testCase = GENERATE(Case{"[Hotkeys]\nNewMark=NotAKey\n"}, Case{"[Hotkeys]\nGlobal=maybe\n"},
                                    Case{"[Skip]\nAutoEnable=nope\n"}, Case{"[Osd]\nEnabled=nope\n"},
                                    Case{"[Osd]\nDurationMs=-5\n"}, Case{"[Osd]\nDurationMs=abc\n"},
                                    Case{"[Log]\nVerbosity=Loud\n"});

    CAPTURE(testCase.ini);
    const auto parsed = ParseConfig(testCase.ini);
    REQUIRE(parsed.warnings.size() == 1);
    // Every field not targeted by the malformed line above stays default.
    REQUIRE(parsed.config.newMarkHotkey == DefaultConfig().newMarkHotkey);
}

TEST_CASE("ParseConfig warns once on an unknown section and ignores every key under it",
          "[core][config]") {
    const auto parsed = ParseConfig("[Bogus]\nFoo=bar\nBaz=qux\n");
    REQUIRE(parsed.warnings.size() == 1);
    REQUIRE(parsed.config.newMarkHotkey == DefaultConfig().newMarkHotkey);
}

TEST_CASE("ParseConfig warns on an unknown key within a known section", "[core][config]") {
    const auto parsed = ParseConfig("[Log]\nBogusKey=1\n");
    REQUIRE(parsed.warnings.size() == 1);
    REQUIRE(parsed.config.logVerbosity == DefaultConfig().logVerbosity);
}

TEST_CASE("ParseConfig warns on a key found before any section", "[core][config]") {
    const auto parsed = ParseConfig("Verbosity=Warn\n[Log]\n");
    REQUIRE(parsed.warnings.size() == 1);
    REQUIRE(parsed.config.logVerbosity == DefaultConfig().logVerbosity);
}

TEST_CASE("ParseConfig warns on a malformed line with no '='", "[core][config]") {
    const auto parsed = ParseConfig("[Log]\nnonsense line\n");
    REQUIRE(parsed.warnings.size() == 1);
}

TEST_CASE("SerializeConfig(DefaultConfig()) round-trips through ParseConfig with no warnings",
          "[core][config]") {
    const Config original = DefaultConfig();
    const std::string text = SerializeConfig(original);
    const auto parsed = ParseConfig(text);

    REQUIRE(parsed.warnings.empty());
    REQUIRE(parsed.config.newMarkHotkey == original.newMarkHotkey);
    REQUIRE(parsed.config.markStartHotkey == original.markStartHotkey);
    REQUIRE(parsed.config.markEndHotkey == original.markEndHotkey);
    REQUIRE(parsed.config.globalHotkeys == original.globalHotkeys);
    REQUIRE(parsed.config.autoEnableSkip == original.autoEnableSkip);
    REQUIRE(parsed.config.osdEnabled == original.osdEnabled);
    REQUIRE(parsed.config.osdDurationMs == original.osdDurationMs);
    REQUIRE(parsed.config.logVerbosity == original.logVerbosity);
}

TEST_CASE("SerializeConfig round-trips a fully non-default config too", "[core][config]") {
    Config original;
    original.newMarkHotkey = HotkeySpec{core::kModControl | core::kModShift, '9'};
    original.markStartHotkey = HotkeySpec{core::kModWin, 'Q'};
    original.markEndHotkey = HotkeySpec{0, 'Z'};
    original.globalHotkeys = true;
    original.autoEnableSkip = false;
    original.osdEnabled = false;
    original.osdDurationMs = 42;
    original.logVerbosity = diagnostics::Severity::Warn;

    const auto parsed = ParseConfig(SerializeConfig(original));
    REQUIRE(parsed.warnings.empty());
    REQUIRE(parsed.config.newMarkHotkey == original.newMarkHotkey);
    REQUIRE(parsed.config.markStartHotkey == original.markStartHotkey);
    REQUIRE(parsed.config.markEndHotkey == original.markEndHotkey);
    REQUIRE(parsed.config.globalHotkeys == original.globalHotkeys);
    REQUIRE(parsed.config.autoEnableSkip == original.autoEnableSkip);
    REQUIRE(parsed.config.osdEnabled == original.osdEnabled);
    REQUIRE(parsed.config.osdDurationMs == original.osdDurationMs);
    REQUIRE(parsed.config.logVerbosity == original.logVerbosity);
}
