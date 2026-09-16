#include <catch_amalgamated.hpp>

#include "core/core.hpp"

using core::ParseError;
using core::SkipRange;

TEST_CASE("SkipRange::Create computes length as end - start", "[core][skip_range]") {
    struct Case {
        core::Milliseconds start;
        core::Milliseconds end;
        core::Milliseconds expectedLength;
    };

    const auto [start, end, expectedLength] = GENERATE(
        Case{0, 1, 1},
        Case{754567, 1425678, 671111},
        Case{41074, 68101, 27027},
        Case{1306906, 1672138, 365232}
    );

    CAPTURE(start, end);
    const SkipRange range = SkipRange::Create(start, end);
    REQUIRE(range.StartMs() == start);
    REQUIRE(range.EndMs() == end);
    REQUIRE(range.LengthMs() == expectedLength);
}

TEST_CASE("SkipRange::Create rejects a zero-length range", "[core][skip_range]") {
    REQUIRE_THROWS_AS(SkipRange::Create(1000, 1000), ParseError);
}

TEST_CASE("SkipRange::Create rejects a backwards range", "[core][skip_range]") {
    REQUIRE_THROWS_AS(SkipRange::Create(1000, 500), ParseError);
}

TEST_CASE("SkipRange::Create rejects a negative start", "[core][skip_range]") {
    REQUIRE_THROWS_AS(SkipRange::Create(-1, 500), ParseError);
}

// Negative case: pins down that a negative end alone (with an equally
// negative-adjacent but still-backwards start) is still caught by the
// start check, not silently accepted because the length looks positive.
TEST_CASE("SkipRange::Create rejects a negative end", "[core][skip_range]") {
    REQUIRE_THROWS_AS(SkipRange::Create(-500, -1), ParseError);
}
