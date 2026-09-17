#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

#include "core/core.hpp"
#include "plugin/skip_marking.hpp"

using core::Milliseconds;
using core::SkipRange;
using plugin::SkipMarkingDriver;
using plugin::SkipMarkingStateMachine;

namespace {

// A synthetic player: fileOpen/positionMs are set directly by the test to
// script (key, position, fileOpen) event sequences, and every commitRange
// call the state machine makes lands in `committed` (or is dropped if
// `nextCommitSucceeds` is false), never touching a real window or dialog.
struct FakePlayer {
    bool fileOpen = true;
    Milliseconds positionMs = 0;
    bool nextCommitSucceeds = true;
    std::vector<SkipRange> committed;
    std::vector<std::string> osdMessages;

    SkipMarkingDriver Driver() {
        return SkipMarkingDriver{
            .isFileOpen = [this] { return fileOpen; },
            .getPositionMs = [this] { return positionMs; },
            .commitRange =
                [this](const SkipRange& range) {
                    if (!nextCommitSucceeds) {
                        return false;
                    }
                    committed.push_back(range);
                    return true;
                },
            .showOsd = [this](const std::string& text) { osdMessages.push_back(text); },
        };
    }
};

}  // namespace

TEST_CASE("Alt+A, Alt+[, Alt+] commits exactly one range at the captured positions",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 5000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 5000);
}

TEST_CASE("A second Alt+A starts a fresh entry without disturbing the first", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();

    machine.OnAltA();
    player.positionMs = 8000;
    machine.OnAltOpenBracket();
    player.positionMs = 9000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 2);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 2000);
    REQUIRE(player.committed[1].StartMs() == 8000);
    REQUIRE(player.committed[1].EndMs() == 9000);
}

TEST_CASE("Repeated start-key presses before the end is set keep updating the start",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltOpenBracket();  // overwrites the start, still no end set yet
    player.positionMs = 5000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 2000);
    REQUIRE(player.committed[0].EndMs() == 5000);
}

TEST_CASE("Repeated end-key presses before the start is set keep updating the end",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 3000;
    machine.OnAltCloseBracket();  // end set first, no start yet: no commit
    player.positionMs = 4000;
    machine.OnAltCloseBracket();  // overwrites the end, still no start set
    player.positionMs = 1000;
    machine.OnAltOpenBracket();  // completes and commits the pair

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 4000);
}

TEST_CASE("Alt+[ or Alt+] with no active entry auto-starts one", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    // No Alt+A at all.
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 6000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 6000);
}

TEST_CASE("Every key is ignored while no file is open", "[plugin][skip_marking]") {
    FakePlayer player;
    player.fileOpen = false;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.empty());

    // Once a file is open, the same key sequence behaves normally.
    player.fileOpen = true;
    machine.OnAltA();
    player.positionMs = 3000;
    machine.OnAltOpenBracket();
    player.positionMs = 4000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 3000);
    REQUIRE(player.committed[0].EndMs() == 4000);
}

TEST_CASE("An end at or before the current start is rejected, not committed", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 5000;
    machine.OnAltOpenBracket();
    player.positionMs = 5000;  // equal to start: rejected
    machine.OnAltCloseBracket();
    player.positionMs = 4000;  // before start: rejected
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.empty());

    player.positionMs = 9000;  // finally after start: commits
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 5000);
    REQUIRE(player.committed[0].EndMs() == 9000);
}

TEST_CASE("A start at or after the current end is rejected, not committed", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 5000;
    machine.OnAltCloseBracket();
    player.positionMs = 5000;  // equal to end: rejected
    machine.OnAltOpenBracket();
    player.positionMs = 6000;  // after end: rejected
    machine.OnAltOpenBracket();

    REQUIRE(player.committed.empty());

    player.positionMs = 1000;  // finally before end: commits
    machine.OnAltOpenBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 5000);
}

TEST_CASE("A failed commit still closes the entry, so the next one starts clean",
          "[plugin][skip_marking]") {
    FakePlayer player;
    player.nextCommitSucceeds = false;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.empty());

    player.nextCommitSucceeds = true;
    machine.OnAltA();
    player.positionMs = 7000;
    machine.OnAltOpenBracket();
    player.positionMs = 8000;
    machine.OnAltCloseBracket();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 7000);
    REQUIRE(player.committed[0].EndMs() == 8000);
}

TEST_CASE("The normal Alt+A/[/] sequence shows exactly one OSD line per keypress",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 754567;
    machine.OnAltOpenBracket();
    player.positionMs = 1425678;
    machine.OnAltCloseBracket();

    REQUIRE(player.osdMessages == std::vector<std::string>{
                                       "Skip: new mark",
                                       "Skip start 00:12:34",
                                       "Skip 00:12:34 – 00:23:45 saved",
                                   });
}

TEST_CASE("Setting the end before the start shows 'Skip end', not a premature save",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 4000;
    machine.OnAltCloseBracket();  // no start yet: shows "end set", no commit

    REQUIRE(player.osdMessages == std::vector<std::string>{"Skip end 00:00:04"});
    REQUIRE(player.committed.empty());
}

TEST_CASE("A hotkey with no file open shows 'No file open' instead of acting",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    player.fileOpen = false;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    machine.OnAltOpenBracket();
    machine.OnAltCloseBracket();

    REQUIRE(player.osdMessages == std::vector<std::string>{"No file open", "No file open", "No file open"});
}

TEST_CASE("An out-of-order bound that would invert the range shows 'ignored', not a silent no-op",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 5000;
    machine.OnAltOpenBracket();
    player.positionMs = 4000;  // before start: rejected
    machine.OnAltCloseBracket();

    REQUIRE(player.osdMessages.back() == "Skip mark ignored (invalid range)");
}

TEST_CASE("A failed commit shows \"Couldn't save mark\"", "[plugin][skip_marking][osd]") {
    FakePlayer player;
    player.nextCommitSucceeds = false;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();

    REQUIRE(player.osdMessages.back() == "Couldn't save mark");
}
