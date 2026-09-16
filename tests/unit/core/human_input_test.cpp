#include <catch_amalgamated.hpp>

#include "core/core.hpp"

using core::ParseError;
using core::ParseHumanRange;
using core::ParseHumanTimecode;

TEST_CASE("ParseHumanTimecode accepts SS, MM:SS, and HH:MM:SS, with optional .mmm",
          "[core][human_input]") {
    struct Case {
        const char* text;
        core::Milliseconds expected;
    };

    const auto [text, expected] = GENERATE(
        Case{"5", 5000},
        Case{"90", 90000},
        Case{"5.5", 5500},
        Case{"5.500", 5500},
        Case{"1:30", 90000},
        Case{"01:30", 90000},
        Case{"1:30.25", 90250},
        Case{"1:02:03", 3723000},
        Case{"01:02:03.5", 3723500},
        Case{"12:34:56.123", 45296123}
    );

    CAPTURE(text);
    REQUIRE(ParseHumanTimecode(text) == expected);
}

// A total-seconds "SS" input and its MM:SS equivalent must agree exactly —
// the same instant expressed two ways.
TEST_CASE("ParseHumanTimecode agrees between equivalent SS and MM:SS forms", "[core][human_input]") {
    REQUIRE(ParseHumanTimecode("90") == ParseHumanTimecode("1:30"));
}

TEST_CASE("ParseHumanTimecode rejects garbage with a clear error, never coercing to 0",
          "[core][human_input]") {
    const std::string text =
        GENERATE(std::string("banana"), std::string(""), std::string("   "), std::string("-5"),
                 std::string("1:2:3:4"),       // too many fields
                 std::string("1:60"),          // MM:SS seconds out of range
                 std::string("60:00"),         // MM:SS minutes out of range
                 std::string("1:60:00"),       // HH:MM:SS minutes out of range
                 std::string("1:00:60"),       // HH:MM:SS seconds out of range
                 std::string("1:00:00.1234"),  // too many millisecond digits
                 std::string("1:00:00."),      // empty fraction
                 std::string("1::00"));        // empty field

    CAPTURE(text);
    REQUIRE_THROWS_AS(ParseHumanTimecode(text), ParseError);
}

TEST_CASE("ParseHumanRange accepts '-', '~', and '->' separators", "[core][human_input]") {
    struct Case {
        const char* text;
        core::Milliseconds expectedStart;
        core::Milliseconds expectedEnd;
    };

    const auto [text, expectedStart, expectedEnd] = GENERATE(
        Case{"10-20", 10000, 20000},
        Case{"1:00~2:00", 60000, 120000},
        Case{"1:00->2:00", 60000, 120000},
        Case{" 10 - 20 ", 10000, 20000}
    );

    CAPTURE(text);
    const core::SkipRange range = ParseHumanRange(text);
    REQUIRE(range.StartMs() == expectedStart);
    REQUIRE(range.EndMs() == expectedEnd);
}

TEST_CASE("ParseHumanRange rejects backwards, zero-length, and garbage input", "[core][human_input]") {
    const std::string text =
        GENERATE(std::string("banana"), std::string(""), std::string("20-10"),  // backwards
                 std::string("10-10"),                                          // zero-length
                 std::string("10"),                                             // no separator
                 std::string("-10"),                                            // missing start
                 std::string("10-"));                                           // missing end

    CAPTURE(text);
    REQUIRE_THROWS_AS(ParseHumanRange(text), ParseError);
}
