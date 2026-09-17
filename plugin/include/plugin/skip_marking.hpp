#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/core.hpp"

// PTS-014: ties the three PTS-013 hotkeys to PTS-011's add-range primitive
// via a small state machine — "what are the pending start/end, and has the
// user committed them yet?" — kept <windows.h>-free like plugin/hotkeys.hpp
// and plugin/window.hpp so the sequencing/guard logic here is unit-testable
// against synthetic event streams, no live PotPlayer window or dialog
// needed. The live half (MakeLiveSkipMarkingDriver, in skip_marking.cpp) is
// the only place this feature touches plugin::IsFileOpen/GetPositionMs/
// OpenSkipSetup/etc.
namespace plugin {

// Everything the state machine needs from the live player, injected as
// callables — the same std::function-per-operation pattern
// ResolveSkipSetupControls' `lookup` and RunClearAllLoop's `deleteFirst`
// already use elsewhere in this codebase — so a test can supply lambdas
// over synthetic position/file-open state and a captured list of "what got
// committed", with no real window, dialog, or SendMessage anywhere in the
// test binary.
struct SkipMarkingDriver {
    // PTS-017: nullopt if this PotPlayer build still matches every
    // assumption docs/FINDINGS.md's dialog-geography section measured, or a
    // logged-and-shown detail string on the first mismatch found. Checked
    // before every other guard in every handler below — a version drift is
    // refused the same "logged, shown, never a guessed write" way "no file
    // open" is, rather than only surfacing once AddFileSpecificSkipRange
    // happens to be reached. The live driver runs the actual check at most
    // once per process and caches the result (plugin::EnsureSelfCheckPassed
    // — this issue's own "one-time validation on first hotkey use" design
    // note), so this callback is cheap on every call after the first.
    std::function<std::optional<std::string>()> checkVersionSupport;
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

// Pick-then-commit: Alt+[ and Alt+] only ever edit a **pending** mark held
// in memory — never PotPlayer's own list — so either can be pressed any
// number of times, in any order, to dial the mark in before anything is
// written. Alt+A is the one key that writes anything: it takes whatever the
// pending start/end currently are and drives `driver.commitRange` exactly
// once. This is a deliberate reversal of PTS-014's original ordering
// (Alt+A start entry -> Alt+[ -> Alt+] commit-on-complete), which the owner
// found unworkable once Alt+] fired: the entry was written immediately and
// could no longer be adjusted (see issue #18's follow-up).
//
// A successful commit clears the pending mark so the next Alt+[/Alt+]
// describes a fresh one. A *failed* commitRange call deliberately leaves the
// pending mark in place — PTS-011/012 only expose an additive Add, no
// update-in-place, so there is nothing to "undo" — letting the same Alt+A be
// retried with no need to re-press Alt+[/Alt+].
class SkipMarkingStateMachine {
public:
    explicit SkipMarkingStateMachine(SkipMarkingDriver driver);

    // Alt+[: sets the pending mark's start to the current playback
    // position, always overwriting whatever was there before. Refuses
    // (logged), leaving the pending mark untouched, if no file is open —
    // PTS-009's IsFileOpen() — rather than marking into the void. Also
    // refuses if PTS-017's self-check found this PotPlayer build
    // unsupported (checked first, ahead of every other guard here).
    void OnAltOpenBracket();

    // Alt+]: sets the pending mark's end to the current playback position,
    // mirroring OnAltOpenBracket.
    void OnAltCloseBracket();

    // Alt+A: commits the pending start/end as one new File-specific range
    // for the currently open file. Refuses (logged + OSD), leaving the
    // pending mark untouched so it can be fixed and retried, if: PTS-017's
    // self-check found this PotPlayer build unsupported; no file is open;
    // the pending start and/or end hasn't been set yet; or the pending end
    // is not strictly after the pending start (PotPlayer requires
    // end > start).
    void OnAltA();

private:
    struct PendingMark {
        std::optional<core::Milliseconds> startMs;
        std::optional<core::Milliseconds> endMs;
    };

    SkipMarkingDriver driver_;
    PendingMark pending_;
};

}
