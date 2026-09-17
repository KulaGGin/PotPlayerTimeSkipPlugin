#include <catch_amalgamated.hpp>

#include <optional>
#include <string>

#include "core/core.hpp"
#include "plugin/osd.hpp"

using core::Milliseconds;
using plugin::ComposeOsdText;
using plugin::OsdEvent;
using plugin::OsdMessage;

// Table-driven: event + times -> display text, per the PTS-015 "Tests"
// section. No live player or window involved — plugin::ShowOsdLive (the
// only windows.h-touching half of osd.cpp) is exercised manually, not here.
TEST_CASE("ComposeOsdText renders each event as the issue's example text", "[plugin][osd]") {
    struct Case {
        OsdMessage message;
        const char* expected;
    };

    const auto testCase = GENERATE(
        Case{OsdMessage{OsdEvent::kNoFileOpen}, "No file open"},
        Case{OsdMessage{OsdEvent::kMarkIgnored}, "Skip mark ignored (invalid range)"},
        Case{OsdMessage{OsdEvent::kMarkIncomplete}, "Set start and end first"},
        Case{OsdMessage{OsdEvent::kMarkStart, Milliseconds{754567}, std::nullopt}, "Skip start 00:12:34"},
        Case{OsdMessage{OsdEvent::kMarkEnd, std::nullopt, Milliseconds{1425678}}, "Skip end 00:23:45"},
        Case{OsdMessage{OsdEvent::kSaved, Milliseconds{754567}, Milliseconds{1425678}},
             "Skip 00:12:34 – 00:23:45 saved"},
        Case{OsdMessage{OsdEvent::kSaveFailed}, "Couldn't save mark"}
    );

    CAPTURE(static_cast<int>(testCase.message.event));
    REQUIRE(ComposeOsdText(testCase.message) == testCase.expected);
}
