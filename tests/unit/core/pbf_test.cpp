#include <catch_amalgamated.hpp>

#include "core/core.hpp"

using core::BuildPlaySkipSection;
using core::ParseError;
using core::ParsePbfLine;
using core::ParsePlaySkipSection;
using core::PbfEntry;
using core::SerializePbfLine;
using core::SkipRange;
using core::kFileSpecificType;

TEST_CASE("SerializePbfLine matches the captured .pbf format", "[core][pbf]") {
    // docs/FINDINGS.md section 1, first captured example: Start 00:12:34.567
    // (754567 ms), Length 671.111s (671111 ms), Type = File-specific.
    const PbfEntry entry{0, kFileSpecificType, SkipRange::Create(754567, 754567 + 671111)};
    REQUIRE(SerializePbfLine(entry) == "0=1*754567*671111");
}

TEST_CASE("ParsePbfLine parses the captured .pbf format", "[core][pbf]") {
    const PbfEntry entry = ParsePbfLine("0=1*754567*671111");
    REQUIRE(entry.index == 0);
    REQUIRE(entry.type == kFileSpecificType);
    REQUIRE(entry.range.StartMs() == 754567);
    REQUIRE(entry.range.LengthMs() == 671111);
    // Field 3 is a length, not an end time: this is the signature failure
    // mode called out in docs/FINDINGS.md — pin the derived end explicitly.
    REQUIRE(entry.range.EndMs() == 1425678);
}

TEST_CASE(".pbf line serialize/parse round-trips", "[core][pbf]") {
    struct Case {
        int index;
        int type;
        core::Milliseconds start;
        core::Milliseconds length;
    };

    const auto [index, type, start, length] = GENERATE(
        Case{0, 1, 754567, 671111},
        Case{1, 1, 477610, 27027},
        Case{10, 1, 1306906, 365232},
        Case{0, 0, 0, 1}
    );

    const PbfEntry original{index, type, SkipRange::Create(start, start + length)};
    const std::string line = SerializePbfLine(original);
    const PbfEntry parsed = ParsePbfLine(line);

    REQUIRE(parsed.index == original.index);
    REQUIRE(parsed.type == original.type);
    REQUIRE(parsed.range.StartMs() == original.range.StartMs());
    REQUIRE(parsed.range.LengthMs() == original.range.LengthMs());
}

TEST_CASE("ParsePbfLine rejects the terminator line", "[core][pbf]") {
    REQUIRE_THROWS_AS(ParsePbfLine("1="), ParseError);
}

TEST_CASE("ParsePbfLine rejects malformed lines", "[core][pbf]") {
    const std::string line =
        GENERATE(std::string(""), std::string("banana"), std::string("0*1*754567*671111"),
                 std::string("0=1*754567"), std::string("0=1*754567*"), std::string("0=1*-1*671111"));

    CAPTURE(line);
    REQUIRE_THROWS_AS(ParsePbfLine(line), ParseError);
}

// Golden test: the whole [PlaySkip] section built from this library must
// match docs/FINDINGS.md's real captured single-range example byte-for-byte
// (the UTF-16LE+BOM encoding is the I/O layer's job; this is the text
// content that sits inside that encoding).
TEST_CASE("BuildPlaySkipSection matches the captured single-range golden .pbf", "[core][pbf]") {
    const std::vector<SkipRange> ranges{SkipRange::Create(754567, 754567 + 671111)};
    REQUIRE(BuildPlaySkipSection(ranges) == "[PlaySkip]\r\n0=1*754567*671111\r\n1=\r\n");
}

// Golden test: the second captured example in docs/FINDINGS.md, 11 ranges
// verified byte-for-byte against requested timecodes. Only the first, an
// arbitrary middle, and the last entries are reproduced here (the full
// capture is elided with "..." in the source document), but the terminator
// index must still land on 11.
TEST_CASE("BuildPlaySkipSection places the terminator after the last index", "[core][pbf]") {
    const std::vector<SkipRange> ranges{
        SkipRange::Create(41074, 41074 + 11111),
        SkipRange::Create(477610, 477610 + 27027),
        SkipRange::Create(1306906, 1306906 + 365232),
    };
    const std::string section = BuildPlaySkipSection(ranges);
    REQUIRE(section == "[PlaySkip]\r\n"
                        "0=1*41074*11111\r\n"
                        "1=1*477610*27027\r\n"
                        "2=1*1306906*365232\r\n"
                        "3=\r\n");
}

TEST_CASE("ParsePlaySkipSection parses the captured golden .pbf", "[core][pbf]") {
    const std::vector<SkipRange> ranges = ParsePlaySkipSection("[PlaySkip]\r\n0=1*754567*671111\r\n1=\r\n");

    REQUIRE(ranges.size() == 1);
    REQUIRE(ranges[0].StartMs() == 754567);
    REQUIRE(ranges[0].LengthMs() == 671111);
}

TEST_CASE("ParsePlaySkipSection is tolerant of the empty terminator line", "[core][pbf]") {
    // No trailing newline after the terminator, mirroring how a hand-edited
    // or truncated file might look; still parses cleanly.
    const std::vector<SkipRange> ranges = ParsePlaySkipSection("[PlaySkip]\r\n0=1*100*50\r\n1=");
    REQUIRE(ranges.size() == 1);
}

TEST_CASE("[PlaySkip] section round-trips through build then parse", "[core][pbf]") {
    const std::vector<SkipRange> original{
        SkipRange::Create(41074, 41074 + 11111),
        SkipRange::Create(477610, 477610 + 27027),
        SkipRange::Create(1306906, 1306906 + 365232),
    };

    const std::vector<SkipRange> roundTripped = ParsePlaySkipSection(BuildPlaySkipSection(original));

    REQUIRE(roundTripped.size() == original.size());
    for (std::size_t i = 0; i < original.size(); ++i) {
        CAPTURE(i);
        REQUIRE(roundTripped[i].StartMs() == original[i].StartMs());
        REQUIRE(roundTripped[i].LengthMs() == original[i].LengthMs());
    }
}

TEST_CASE("ParsePlaySkipSection rejects a malformed data line", "[core][pbf]") {
    REQUIRE_THROWS_AS(ParsePlaySkipSection("[PlaySkip]\r\n0=banana\r\n1=\r\n"), ParseError);
}
