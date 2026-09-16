#include <catch_amalgamated.hpp>

#include "core/core.hpp"

using core::FormatTimecode;
using core::ParseError;
using core::ParseTimecode;

// Table-driven: every row is an independent Catch2 case, per the pattern in
// tests/unit/core/add_test.cpp.
TEST_CASE("FormatTimecode renders ms as HH:MM:SS.mmm", "[core][timecode]") {
    struct Case {
        core::Milliseconds ms;
        const char* expected;
    };

    const auto [ms, expected] = GENERATE(
        Case{0, "00:00:00.000"},
        Case{999, "00:00:00.999"},
        Case{1000, "00:00:01.000"},
        Case{671111, "00:11:11.111"},
        Case{754567, "00:12:34.567"},
        // Multi-hour: field 3 of the golden second capture in FINDINGS.md
        // (index 10) is 1306906 ms start, well past an hour.
        Case{1306906, "00:21:46.906"},
        Case{3661001, "01:01:01.001"},
        Case{360000000, "100:00:00.000"}
    );

    CAPTURE(ms);
    REQUIRE(FormatTimecode(ms) == expected);
}

TEST_CASE("FormatTimecode rejects a negative duration", "[core][timecode]") {
    REQUIRE_THROWS_AS(FormatTimecode(-1), ParseError);
}

TEST_CASE("ParseTimecode parses HH:MM:SS.mmm to ms", "[core][timecode]") {
    struct Case {
        const char* text;
        core::Milliseconds expected;
    };

    const auto [text, expected] = GENERATE(
        Case{"00:00:00.000", 0},
        Case{"00:00:00.999", 999},
        Case{"00:00:01.000", 1000},
        Case{"00:11:11.111", 671111},
        Case{"00:12:34.567", 754567},
        Case{"01:01:01.001", 3661001},
        Case{"100:00:00.000", 360000000}
    );

    CAPTURE(text);
    REQUIRE(ParseTimecode(text) == expected);
}

// Round-trip: ms -> text -> ms across a spread of values including
// sub-second and multi-hour, per the PTS-008 acceptance criteria.
TEST_CASE("ms <-> HH:MM:SS.mmm round-trips exactly", "[core][timecode]") {
    const core::Milliseconds ms = GENERATE(0, 1, 999, 1000, 60000, 671111, 754567, 3599999, 3661001,
                                            359999999, 360000000, 1'000'000'000LL);

    CAPTURE(ms);
    REQUIRE(ParseTimecode(FormatTimecode(ms)) == ms);
}

TEST_CASE("ParseTimecode rejects malformed input with a clear error", "[core][timecode]") {
    const std::string text = GENERATE(std::string("banana"), std::string(""), std::string("12:34"),
                                       std::string("12:34:56"),            // missing .mmm
                                       std::string("12:34:56.12"),         // too few millis digits
                                       std::string("12:34:56.1234"),       // too many millis digits
                                       std::string("12:3:56.123"),         // minutes not 2 digits
                                       std::string("12:34:5.123"),         // seconds not 2 digits
                                       std::string("12:60:00.000"),        // minutes out of range
                                       std::string("12:00:60.000"),        // seconds out of range
                                       std::string(":34:56.123"),          // empty hours
                                       std::string("ab:34:56.123"));       // non-digit hours

    CAPTURE(text);
    REQUIRE_THROWS_AS(ParseTimecode(text), ParseError);
}
