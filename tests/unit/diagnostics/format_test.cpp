#include <catch_amalgamated.hpp>

#include "diagnostics/format.hpp"

using diagnostics::FormatLine;
using diagnostics::Severity;
using diagnostics::SeverityTag;
using diagnostics::Timestamp;

namespace {
constexpr Timestamp kFixedTimestamp{2026, 9, 16, 14, 5, 9, 42};
}

// Table-driven: every row is an independent Catch2 case, per the pattern in
// tests/unit/core/add_test.cpp.
TEST_CASE("FormatLine produces one line per event with the expected fields", "[diagnostics][format]") {
    struct Case {
        Severity severity;
        const char* tag;
    };

    const auto [severity, tag] = GENERATE(
        Case{Severity::Info, "INFO "},
        Case{Severity::Warn, "WARN "},
        Case{Severity::Error, "ERROR"}
    );

    CAPTURE(tag);
    const std::string line = FormatLine(severity, kFixedTimestamp, 4321, "something happened");

    // Exactly one line: no embedded newlines except the single trailing one.
    REQUIRE(line.find('\n') == line.size() - 1);
    REQUIRE(line.back() == '\n');

    REQUIRE(line.find("[2026-09-16 14:05:09.042]") != std::string::npos);
    REQUIRE(line.find("[tid 4321]") != std::string::npos);
    REQUIRE(line.find(std::string("[") + tag + "]") != std::string::npos);
    REQUIRE(line.find("something happened") != std::string::npos);
}

// Negative case: pins down that severities are not mixed up with each
// other's tag, so a broken SeverityTag mapping fails loudly here.
TEST_CASE("FormatLine does not tag a line with the wrong severity", "[diagnostics][format]") {
    const std::string infoLine = FormatLine(Severity::Info, kFixedTimestamp, 1, "msg");
    REQUIRE(infoLine.find("[WARN ]") == std::string::npos);
    REQUIRE(infoLine.find("[ERROR]") == std::string::npos);
}

TEST_CASE("SeverityTag maps every severity to a distinct, fixed-width tag", "[diagnostics][format]") {
    REQUIRE(SeverityTag(Severity::Info) == "INFO ");
    REQUIRE(SeverityTag(Severity::Warn) == "WARN ");
    REQUIRE(SeverityTag(Severity::Error) == "ERROR");
    REQUIRE(SeverityTag(Severity::Info).size() == SeverityTag(Severity::Error).size());
}

// Non-ASCII round-trips as UTF-8 bytes: the message is passed through
// byte-for-byte, so a path/tag containing an en dash or Cyrillic must come
// back out exactly as it went in. Direct lesson from FINDINGS.md: a single
// en-dash written as text on Windows previously broke an entire log view.
TEST_CASE("FormatLine round-trips non-ASCII UTF-8 content without corruption", "[diagnostics][format]") {
    const std::string message = GENERATE(
        std::string("caf\xC3\xA9.mp4"),                             // "café.mp4"
        std::string("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), // "привет"
        std::string("intro \xE2\x80\x93 outro")                     // "intro – outro"
    );

    const std::string line = FormatLine(Severity::Info, kFixedTimestamp, 1, message);
    REQUIRE(line.find(message) != std::string::npos);
}
