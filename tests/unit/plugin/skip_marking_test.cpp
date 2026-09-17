#include <catch_amalgamated.hpp>

#include <optional>
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
    std::optional<std::string> versionUnsupported;
    std::vector<SkipRange> committed;
    std::vector<std::string> osdMessages;

    SkipMarkingDriver Driver() {
        return SkipMarkingDriver{
            .checkVersionSupport = [this] { return versionUnsupported; },
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

TEST_CASE("Alt+[, Alt+], Alt+A commits exactly one range at the captured positions",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 5000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 5000);
}

TEST_CASE("Alt+[ and Alt+] can be pressed in either order, any number of times, before Alt+A",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 9000;
    machine.OnAltCloseBracket();  // end set first
    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltOpenBracket();  // overwrites the pending start again
    player.positionMs = 5000;
    machine.OnAltCloseBracket();  // overwrites the pending end again
    machine.OnAltA();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 2000);
    REQUIRE(player.committed[0].EndMs() == 5000);
}

TEST_CASE("A second, independent Alt+[/Alt+]/Alt+A sequence commits a second range",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    player.positionMs = 8000;
    machine.OnAltOpenBracket();
    player.positionMs = 9000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.size() == 2);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 2000);
    REQUIRE(player.committed[1].StartMs() == 8000);
    REQUIRE(player.committed[1].EndMs() == 9000);
}

TEST_CASE("After a commit, Alt+A with no new Alt+[/Alt+] is refused, not a duplicate commit",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();
    machine.OnAltA();  // pending mark was cleared by the first commit

    REQUIRE(player.committed.size() == 1);
}

TEST_CASE("Alt+A before either bound is set is refused", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();

    REQUIRE(player.committed.empty());
}

TEST_CASE("Alt+A with only the start set is refused", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    machine.OnAltA();

    REQUIRE(player.committed.empty());
}

TEST_CASE("Alt+A with only the end set is refused", "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 5000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.empty());
}

TEST_CASE("Every key is ignored while no file is open", "[plugin][skip_marking]") {
    FakePlayer player;
    player.fileOpen = false;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.empty());

    // Once a file is open, the same key sequence behaves normally.
    player.fileOpen = true;
    player.positionMs = 3000;
    machine.OnAltOpenBracket();
    player.positionMs = 4000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 3000);
    REQUIRE(player.committed[0].EndMs() == 4000);
}

TEST_CASE("Alt+A with the pending end at or before the pending start is rejected, and can be fixed",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 5000;
    machine.OnAltOpenBracket();
    player.positionMs = 5000;  // equal to start
    machine.OnAltCloseBracket();
    machine.OnAltA();
    REQUIRE(player.committed.empty());

    player.positionMs = 4000;  // before start
    machine.OnAltCloseBracket();
    machine.OnAltA();
    REQUIRE(player.committed.empty());

    // The pending mark was left in place by both refusals, so fixing just
    // the end and retrying Alt+A commits it.
    player.positionMs = 9000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 5000);
    REQUIRE(player.committed[0].EndMs() == 9000);
}

TEST_CASE("A failed commit leaves the pending mark in place so Alt+A can be retried",
          "[plugin][skip_marking]") {
    FakePlayer player;
    player.nextCommitSucceeds = false;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.empty());

    player.nextCommitSucceeds = true;
    machine.OnAltA();  // retried with no new Alt+[/Alt+]

    REQUIRE(player.committed.size() == 1);
    REQUIRE(player.committed[0].StartMs() == 1000);
    REQUIRE(player.committed[0].EndMs() == 2000);
}

TEST_CASE("The normal Alt+[/Alt+]/Alt+A sequence shows exactly one OSD line per keypress",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 754567;
    machine.OnAltOpenBracket();
    player.positionMs = 1425678;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.osdMessages == std::vector<std::string>{
                                       "Skip start 00:12:34",
                                       "Skip end 00:23:45",
                                       "Skip 00:12:34 – 00:23:45 saved",
                                   });
}

TEST_CASE("Alt+A before both bounds are set shows a distinct 'incomplete' message",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 4000;
    machine.OnAltOpenBracket();
    machine.OnAltA();  // no end set yet

    REQUIRE(player.osdMessages.back() == "Set start and end first");
    REQUIRE(player.committed.empty());
}

TEST_CASE("A hotkey with no file open shows 'No file open' instead of acting",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    player.fileOpen = false;
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltOpenBracket();
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.osdMessages == std::vector<std::string>{"No file open", "No file open", "No file open"});
}

TEST_CASE("Alt+A with an end that would invert the range shows 'ignored', not a silent no-op",
          "[plugin][skip_marking][osd]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 5000;
    machine.OnAltOpenBracket();
    player.positionMs = 4000;  // before start
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.osdMessages.back() == "Skip mark ignored (invalid range)");
}

TEST_CASE("A failed commit shows \"Couldn't save mark\"", "[plugin][skip_marking][osd]") {
    FakePlayer player;
    player.nextCommitSucceeds = false;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.osdMessages.back() == "Couldn't save mark");
}

TEST_CASE("Every key is refused with 'unsupported version' when the self-check has failed",
          "[plugin][skip_marking]") {
    FakePlayer player;
    player.versionUnsupported = "main window (class PotPlayer64) not found";
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.empty());
    REQUIRE(player.osdMessages == std::vector<std::string>{"This PotPlayer version isn't supported",
                                                             "This PotPlayer version isn't supported",
                                                             "This PotPlayer version isn't supported"});
}

TEST_CASE("An unsupported version is checked before the no-file-open guard", "[plugin][skip_marking]") {
    FakePlayer player;
    player.fileOpen = false;
    player.versionUnsupported = "Skip Setup dialog or one of its controls not found";
    SkipMarkingStateMachine machine(player.Driver());

    machine.OnAltA();

    REQUIRE(player.osdMessages == std::vector<std::string>{"This PotPlayer version isn't supported"});
}

TEST_CASE("A supported version (the default) doesn't block the normal Alt+[/Alt+]/Alt+A sequence",
          "[plugin][skip_marking]") {
    FakePlayer player;
    SkipMarkingStateMachine machine(player.Driver());

    player.positionMs = 1000;
    machine.OnAltOpenBracket();
    player.positionMs = 2000;
    machine.OnAltCloseBracket();
    machine.OnAltA();

    REQUIRE(player.committed.size() == 1);
}
