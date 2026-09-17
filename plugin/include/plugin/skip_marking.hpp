#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/core.hpp"

// PTS-014: ties the three PTS-013 hotkeys to PTS-011's add-range primitive
// via a small state machine — "which entry am I currently editing?" — kept
// <windows.h>-free like plugin/hotkeys.hpp and plugin/window.hpp so the
// sequencing/guard logic here is unit-testable against synthetic event
// streams, no live PotPlayer window or dialog needed. The live half
// (MakeLiveSkipMarkingDriver, in skip_marking.cpp) is the only place this
// feature touches plugin::IsFileOpen/GetPositionMs/OpenSkipSetup/etc.
namespace plugin {

// Everything the state machine needs from the live player, injected as
// callables — the same std::function-per-operation pattern
// ResolveSkipSetupControls' `lookup` and RunClearAllLoop's `deleteFirst`
// already use elsewhere in this codebase — so a test can supply lambdas
// over synthetic position/file-open state and a captured list of "what got
// committed", with no real window, dialog, or SendMessage anywhere in the
// test binary.
struct SkipMarkingDriver {
    std::function<bool()> isFileOpen;
    std::function<core::Milliseconds()> getPositionMs;
    // Drives the full open-Setup/enable/add/OK round trip for one range.
    // Returns false (already logged by the caller) if any step failed; the
    // state machine never inspects which step it was.
    std::function<bool(const core::SkipRange&)> commitRange;
    // PTS-015: shows one already-composed OSD line for the event that just
    // happened. Never inspected or awaited by the state machine — same
    // fire-and-forget shape as the LOG_* calls next to every call site.
    std::function<void(const std::string&)> showOsd;
};

// The real driver: plugin::IsFileOpen/GetPositionMs for the queries, and
// plugin::OpenSkipSetup + EnsureSkipEnabled + AddFileSpecificSkipRange +
// CloseSkipSetupOk/Cancel (PTS-010/011/012) chained together for
// commitRange. Constructing this is the only place PTS-014 touches the
// live player.
//
// `autoEnableSkip` gates the EnsureSkipEnabled step (PTS-016's config): if
// false, a range still gets added even while Skip Setup's own "Enable skip
// feature" checkbox is off, rather than this driver flipping it on for the
// user.
SkipMarkingDriver MakeLiveSkipMarkingDriver(bool autoEnableSkip);

// Commit strategy: "commit-on-complete" (the issue's recommended option
// (a)) — the active entry lives only in memory until both its start and
// end are known, at which point it is written through `driver.commitRange`
// exactly once and the entry closes. It is deliberately never reopened
// after that: PTS-011/012 only expose an additive Add and an index-based
// Delete, no update-in-place, and no query exists to find a just-added
// range's list index again afterwards — building that is future work, not
// this ticket's. So the next Alt+[ or Alt+] after a commit (with no
// intervening Alt+A) is treated exactly like "no active entry": it quietly
// starts a fresh one, rather than guessing which row to delete and
// replace. This keeps every write here provably a single,
// read-back-verified Add (PTS-011's own guarantee) — never a guess.
class SkipMarkingStateMachine {
public:
    explicit SkipMarkingStateMachine(SkipMarkingDriver driver);

    // Alt+A: begins a new active entry, discarding any previous
    // uncommitted one (a committed one is already safely written, so it's
    // unaffected). Refuses (logged), leaving any active entry untouched,
    // if no file is open — PTS-009's IsFileOpen() — rather than marking
    // into the void.
    void OnAltA();

    // Alt+[: sets the active entry's start to the current playback
    // position, auto-starting an entry first if none is active. Ignored
    // (logged) if an end is already set and the current position is not
    // strictly before it — never overwrites a valid start with one that
    // would make an inverted or zero-length range.
    void OnAltOpenBracket();

    // Alt+]: sets the active entry's end to the current playback position,
    // mirroring OnAltOpenBracket's guard. Setting the second of the two
    // bounds commits the entry immediately and closes it.
    void OnAltCloseBracket();

private:
    struct ActiveEntry {
        std::optional<core::Milliseconds> startMs;
        std::optional<core::Milliseconds> endMs;
    };

    void StartNewEntry();
    // Commits and closes the active entry if both bounds are now set,
    // showing the saved/failed OSD itself since it's the only place that
    // knows the outcome. Returns whether it did so, so OnAltOpenBracket/
    // OnAltCloseBracket know whether to show their own "start/end set" OSD
    // instead — never both for the same keypress.
    bool TryCommit();

    SkipMarkingDriver driver_;
    std::optional<ActiveEntry> active_;
};

}
