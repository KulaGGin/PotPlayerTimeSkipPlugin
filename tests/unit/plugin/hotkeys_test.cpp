#include <catch_amalgamated.hpp>

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
