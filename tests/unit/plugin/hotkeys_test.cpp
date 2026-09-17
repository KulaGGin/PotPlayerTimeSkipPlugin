#include <catch_amalgamated.hpp>

#include "core/config.hpp"
#include "plugin/hotkeys.hpp"

using plugin::HotkeyAction;
using plugin::ResolveHotkeyAction;

TEST_CASE("ResolveHotkeyAction routes each defined id to its own handler", "[plugin][hotkeys]") {
    struct Case {
        int id;
        HotkeyAction expected;
    };

    const auto testCase = GENERATE(Case{1, HotkeyAction::kAltA}, Case{2, HotkeyAction::kAltOpenBracket},
                                    Case{3, HotkeyAction::kAltCloseBracket});

    CAPTURE(testCase.id);
    const auto action = ResolveHotkeyAction(testCase.id);
    REQUIRE(action.has_value());
    REQUIRE(*action == testCase.expected);
}

TEST_CASE("ResolveHotkeyAction returns nullopt for an id nothing was registered under", "[plugin][hotkeys]") {
    REQUIRE_FALSE(ResolveHotkeyAction(0).has_value());
    REQUIRE_FALSE(ResolveHotkeyAction(4).has_value());
    REQUIRE_FALSE(ResolveHotkeyAction(-1).has_value());
}

TEST_CASE("Every hotkey definition has a distinct nonzero id", "[plugin][hotkeys]") {
    for (const auto& definition : plugin::kHotkeyDefinitions) {
        REQUIRE(definition.id != 0);
    }
    REQUIRE(plugin::kHotkeyDefinitions[0].id != plugin::kHotkeyDefinitions[1].id);
    REQUIRE(plugin::kHotkeyDefinitions[0].id != plugin::kHotkeyDefinitions[2].id);
    REQUIRE(plugin::kHotkeyDefinitions[1].id != plugin::kHotkeyDefinitions[2].id);
}

// PTS-016's core::DefaultConfig() bakes in its own copy of these three
// default bindings (core/ can't depend on plugin/hotkeys.hpp's compile-time
// table), so this guards against the two ever silently drifting apart.
TEST_CASE("core::DefaultConfig's hotkeys match plugin::kHotkeyDefinitions exactly", "[plugin][hotkeys][config]") {
    const core::Config defaults = core::DefaultConfig();
    for (const auto& definition : plugin::kHotkeyDefinitions) {
        core::HotkeySpec expected;
        switch (definition.action) {
        case HotkeyAction::kAltA:
            expected = defaults.newMarkHotkey;
            break;
        case HotkeyAction::kAltOpenBracket:
            expected = defaults.markStartHotkey;
            break;
        case HotkeyAction::kAltCloseBracket:
            expected = defaults.markEndHotkey;
            break;
        }
        CAPTURE(definition.name);
        REQUIRE(expected.modifiers == definition.modifiers);
        REQUIRE(expected.virtualKey == definition.virtualKey);
    }
}
