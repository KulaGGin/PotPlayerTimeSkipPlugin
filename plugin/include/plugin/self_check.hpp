#pragma once

#include <functional>
#include <optional>
#include <string>

// PTS-017: verifies, without writing anything, that this running PotPlayer
// build still matches the dialog-geography assumptions docs/FINDINGS.md
// section 3 measured (the main window class, Skip Setup's controls, Skip
// Interval Setup's controls) — so a PotPlayer update that renumbers a
// control or renames a class is caught with a clear, logged, user-visible
// refusal instead of a guessed write or a crash. Kept <windows.h>-free like
// plugin/skip_marking.hpp: the sequencing logic here (RunSelfCheckSequence)
// is unit-testable against synthetic callback outcomes, no live dialog
// needed; the live half (RunLiveSelfCheck/EnsureSelfCheckPassed, in
// self_check.cpp) is the only place this feature touches
// GetMainWindowHandle/OpenSkipSetup/etc.
namespace plugin {

// Which of the three ordered checks failed — lets a caller (and the log
// line) say *which* measured assumption went stale rather than just
// "something's wrong", the same measured/inferred specificity
// docs/FINDINGS.md itself insists on; also what a re-verification after a
// PotPlayer update needs to know to be a bounded task instead of a fresh
// investigation (see docs/REVERIFICATION_CHECKLIST.md).
enum class SelfCheckStage {
    kMainWindow,
    kSkipSetupDialog,
    kSkipIntervalDialog,
};

struct SelfCheckFailure {
    SelfCheckStage stage;
    std::string detail;
};

// Pure sequencing: runs each stage's check callback in order, stopping at
// the first failure — the same "one missing id already means don't trust
// this layout" contract ResolveSkipSetupControls uses — so a later stage's
// (dialog-opening) callback is never even invoked once an earlier one
// fails. Each callback returns nullopt on success or a detail string on
// failure. Returns nullopt once every stage has passed.
std::optional<SelfCheckFailure> RunSelfCheckSequence(
    const std::function<std::optional<std::string>()>& checkMainWindow,
    const std::function<std::optional<std::string>()>& checkSkipSetupDialog,
    const std::function<std::optional<std::string>()>& checkSkipIntervalDialog);

// Live self-check: confirms the main window resolves, then opens Skip Setup
// and, through it, Skip Interval Setup, resolving every control
// docs/FINDINGS.md section 3 lists — Cancelling both dialogs afterward,
// never committing a probe range. Safe to call more than once, but
// EnsureSelfCheckPassed below only ever actually drives PotPlayer's UI once
// per process.
std::optional<SelfCheckFailure> RunLiveSelfCheck();

// Runs RunLiveSelfCheck() at most once per process and caches the result —
// this issue's own design note: "a one-time validation on first hotkey use
// is fine," not a fresh dialog probe on every keypress. Logs the outcome
// (LOG_INFO with the validated PotPlayer version on success, LOG_ERROR with
// the failing stage/detail otherwise) and records it to
// %LOCALAPPDATA%\PotPlayerTimeSkip\selfcheck.ini so tools/install's status
// command (PTS-007) can surface it too. Returns nullopt if this PotPlayer
// build is supported, or a short user-facing detail string otherwise — this
// is what SkipMarkingDriver.checkVersionSupport (plugin/skip_marking.hpp)
// is wired to.
std::optional<std::string> EnsureSelfCheckPassed();

}
