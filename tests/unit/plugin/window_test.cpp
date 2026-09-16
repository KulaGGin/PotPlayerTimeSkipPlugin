#include <catch_amalgamated.hpp>

#include "plugin/window.hpp"

using plugin::SelectWindow;
using plugin::SelectWindowByTitle;
using plugin::WindowCandidate;

TEST_CASE("SelectWindow prefers a visible match over an earlier invisible one", "[plugin][window]") {
    const std::vector<WindowCandidate> candidates{
        WindowCandidate{1, L"PotPlayer64", false},
        WindowCandidate{2, L"PotPlayer64", true},
    };

    const auto selected = SelectWindow(candidates, L"PotPlayer64");
    REQUIRE(selected.has_value());
    REQUIRE(*selected == 2);
}

TEST_CASE("SelectWindow falls back to an invisible match when nothing visible matches", "[plugin][window]") {
    const std::vector<WindowCandidate> candidates{
        WindowCandidate{1, L"SomeOtherClass", true},
        WindowCandidate{2, L"PotPlayer64", false},
    };

    const auto selected = SelectWindow(candidates, L"PotPlayer64");
    REQUIRE(selected.has_value());
    REQUIRE(*selected == 2);
}

TEST_CASE("SelectWindow ignores class name mismatches", "[plugin][window]") {
    const std::vector<WindowCandidate> candidates{
        WindowCandidate{1, L"PotPlayerMini64", true},
        WindowCandidate{2, L"#32770", true},
    };

    REQUIRE_FALSE(SelectWindow(candidates, L"PotPlayer64").has_value());
}

TEST_CASE("SelectWindow returns nullopt for an empty candidate list", "[plugin][window]") {
    REQUIRE_FALSE(SelectWindow({}, L"PotPlayer64").has_value());
}

// Table-driven: mirrors the pattern in tests/unit/core/timecode_test.cpp.
// Every row picks the expected handle out of the same three-candidate list,
// varying which one is visible and which classes are involved.
TEST_CASE("SelectWindow picks the first visible match among several candidates", "[plugin][window]") {
    struct Case {
        std::vector<WindowCandidate> candidates;
        std::uintptr_t expected;
    };

    const auto testCase = GENERATE(
        Case{{WindowCandidate{1, L"PotPlayer64", true}, WindowCandidate{2, L"PotPlayer64", true},
              WindowCandidate{3, L"PotPlayer64", false}},
             1},
        Case{{WindowCandidate{1, L"PotPlayer64", false}, WindowCandidate{2, L"Other", true},
              WindowCandidate{3, L"PotPlayer64", true}},
             3},
        Case{{WindowCandidate{1, L"Other", true}, WindowCandidate{2, L"Other", true},
              WindowCandidate{3, L"PotPlayer64", false}},
             3}
    );

    CAPTURE(testCase.expected);
    const auto selected = SelectWindow(testCase.candidates, L"PotPlayer64");
    REQUIRE(selected.has_value());
    REQUIRE(*selected == testCase.expected);
}

TEST_CASE("SelectWindowByTitle disambiguates dialogs sharing a window class", "[plugin][window]") {
    const std::vector<WindowCandidate> candidates{
        WindowCandidate{1, L"#32770", true, L"Skip Interval Setup"},
        WindowCandidate{2, L"#32770", true, L"Skip Setup"},
    };

    const auto selected = SelectWindowByTitle(candidates, L"#32770", L"Skip Setup");
    REQUIRE(selected.has_value());
    REQUIRE(*selected == 2);
}

TEST_CASE("SelectWindowByTitle requires both class and title to match", "[plugin][window]") {
    const std::vector<WindowCandidate> candidates{
        WindowCandidate{1, L"#32770", true, L"Skip Setup"},
    };

    REQUIRE_FALSE(SelectWindowByTitle(candidates, L"SomeOtherClass", L"Skip Setup").has_value());
    REQUIRE_FALSE(SelectWindowByTitle(candidates, L"#32770", L"Some Other Title").has_value());
}

TEST_CASE("SelectWindowByTitle returns nullopt for an empty candidate list", "[plugin][window]") {
    REQUIRE_FALSE(SelectWindowByTitle({}, L"#32770", L"Skip Setup").has_value());
}
