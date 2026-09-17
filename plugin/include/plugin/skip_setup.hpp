#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

#include "core/core.hpp"

// Opens and drives PotPlayer's Skip Setup dialog in-process, without
// stealing foreground focus or ever drawing it on screen (PTS-010). Kept
// <windows.h>-free like plugin/window.hpp: the live half (EnumWindows,
// PostMessage, GetDlgItem) lives in skip_setup.cpp, using only narrowed
// (uintptr_t) handles here so the control-resolution logic below stays
// unit-testable against synthetic data, no live dialog needed.
namespace plugin {

// Command id for PotPlayer's Skip Setup menu item ("재생 스킵 설정...",
// Play > Playback Skip > Skip Setup...). Measured by statically parsing the
// MENU/ACCELERATOR resources out of PotPlayer64.dll with
// LOAD_LIBRARY_AS_DATAFILE (docs/FINDINGS.md section 3) — no live probe, no
// synthesized keystroke. Posting this id directly to the main window opens
// Skip Setup exactly like PotPlayer's own menu/accelerator would, and is
// PTS-010's option 1 (preferred over accelerator or keystroke injection).
inline constexpr int kOpenSkipSetupCommandId = 10240;

// Skip Setup's child control ids (docs/FINDINGS.md section 3).
inline constexpr int kEnableCheckboxId = 3034;
inline constexpr int kRangeListId = 3242;
inline constexpr int kAddButtonId = 3024;
inline constexpr int kEditButtonId = 3025;
inline constexpr int kDeleteButtonId = 3026;
inline constexpr int kOkButtonId = 1;
inline constexpr int kCancelButtonId = 2;

// Skip Setup's resolved child controls, narrowed HWNDs (see plugin/window.hpp
// for why this file never names the Win32 type).
struct SkipSetupControls {
    std::uintptr_t enableCheckbox;
    std::uintptr_t rangeList;
    std::uintptr_t addButton;
    std::uintptr_t editButton;
    std::uintptr_t deleteButton;
    std::uintptr_t okButton;
    std::uintptr_t cancelButton;
};

// What ResolveSkipSetupControls reports when a required control isn't found:
// which one, by its constant id above.
struct MissingSkipSetupControl {
    int id;
};

// `lookup(id)` should return 0 for "no such control" — GetDlgItem's own
// not-found return value — so the real caller passes a thin wrapper around
// GetDlgItem, and a test can pass a lambda over a synthetic
// std::unordered_map<int, std::uintptr_t>, no live dialog needed (PTS-010's
// unit-test requirement). Checks controls in table order (docs/FINDINGS.md
// section 3) and reports the first missing one: any single missing control
// already means "don't trust this dialog's layout," so there's no value in
// collecting the rest.
std::variant<SkipSetupControls, MissingSkipSetupControl> ResolveSkipSetupControls(
    const std::function<std::uintptr_t(int)>& lookup);

// One opened, parked Skip Setup dialog: its own handle, its drop-shadow
// window's handle (0 if none was found — parking still succeeds without
// one), and its resolved child controls.
struct SkipSetupDialog {
    std::uintptr_t hwnd;
    std::uintptr_t shadowHwnd;
    SkipSetupControls controls;
};

// Opens Skip Setup by posting its own WM_COMMAND id to the main window
// (never a synthesized keystroke, never a foreground change), waits for the
// dialog to appear, parks it and its PotShadowWnd off-screen (docs/FINDINGS.md
// section 6 — parking, not SW_HIDE, is what keeps a dialog pumping messages
// without ever being drawn), and resolves its child controls. Returns
// nullopt (logged) if the main window can't be found, the dialog never
// appears within the timeout, or any expected control is missing — never a
// partially-valid bundle.
std::optional<SkipSetupDialog> OpenSkipSetup();

// Posts a click on OK/Cancel and waits (bounded) for the dialog to actually
// disappear. PostMessage, never SendMessage — docs/FINDINGS.md section 6:
// a modal child dialog on the other end would otherwise block this thread
// until it, too, closes. Returns false (logged) if the dialog is still
// there when the wait times out.
bool CloseSkipSetupOk(const SkipSetupDialog& dialog);
bool CloseSkipSetupCancel(const SkipSetupDialog& dialog);

// Skip Interval Setup's own child control ids (docs/FINDINGS.md section 3),
// opened via Skip Setup's Add... button above. Its OK/Cancel share the
// numeric ids (1/2) of Skip Setup's own OK/Cancel — a different dialog, so
// no collision — kept as separate names since they're resolved against a
// different HWND.
inline constexpr int kIntervalStartEditId = 3088;
inline constexpr int kIntervalEndEditId = 3092;
inline constexpr int kIntervalLengthEditId = 3091;
inline constexpr int kIntervalTypeComboId = 3012;
inline constexpr int kIntervalOkButtonId = 1;
inline constexpr int kIntervalCancelButtonId = 2;

// Skip Interval Setup's Type combo selection index for File-specific
// (`[0]` Overall, `[1]` File-specific, docs/FINDINGS.md section 3) —
// numerically the same as core::kFileSpecificType, but named separately
// since this is the dialog's own combo index, not the .pbf type field it
// ends up producing.
inline constexpr int kFileSpecificComboIndex = 1;

// Skip Interval Setup's resolved child controls, narrowed HWNDs.
struct SkipIntervalControls {
    std::uintptr_t startEdit;
    std::uintptr_t endEdit;
    std::uintptr_t lengthEdit;
    std::uintptr_t typeCombo;
    std::uintptr_t okButton;
    std::uintptr_t cancelButton;
};

// What ResolveSkipIntervalControls reports when a required control isn't
// found, mirroring MissingSkipSetupControl above.
struct MissingSkipIntervalControl {
    int id;
};

// Same lookup-function contract as ResolveSkipSetupControls above: a real
// caller passes GetDlgItem, a test passes a fake over synthetic data.
std::variant<SkipIntervalControls, MissingSkipIntervalControl> ResolveSkipIntervalControls(
    const std::function<std::uintptr_t(int)>& lookup);

// One field of what was WM_SETTEXT/CB_SETCURSEL'd into Skip Interval Setup
// that didn't read back (via WM_GETTEXT/CB_GETCURSEL) as requested.
struct SkipIntervalMismatch {
    std::string field;
    std::string expected;
    std::string actual;
};

// What Skip Interval Setup's start/end edits and type combo actually
// reported back after being filled in, ready to compare against what was
// requested.
struct SkipIntervalReadback {
    std::string startText;
    std::string endText;
    int typeIndex;
};

// The mandatory read-back-before-commit check (PTS-011 — "measure it,
// don't argue it": a syntactically valid entry at the wrong time looks
// exactly like success). Parses readback.startText/endText with
// core::ParseTimecode and compares the resulting millisecond values — not
// the raw text, so a cosmetic reformatting by the dialog would still count
// as a match — against range's start/end, and compares readback.typeIndex
// against kFileSpecificComboIndex. Checked in that order (start, end,
// type) and returns the first mismatch found, or nullopt if everything
// matches and it is safe to commit. Text that doesn't even parse as a
// timecode is itself reported as a mismatch rather than left to throw —
// PotPlayer rejecting or mangling the input is exactly the failure this
// check exists to catch, not a bug in the caller.
std::optional<SkipIntervalMismatch> VerifySkipIntervalReadback(const core::SkipRange& range,
                                                                const SkipIntervalReadback& readback);

// Drives Skip Setup's Add... through a complete Skip Interval Setup round
// trip to add one File-specific range to the currently open file (PTS-011).
// `dialog` must already be open (OpenSkipSetup()).
//
// Clicks Add... (PostMessage, never SendMessage — its handler runs its own
// modal DialogBox loop, docs/FINDINGS.md section 6), waits for Skip
// Interval Setup to appear, parks it off-screen, resolves its controls,
// fills in the requested start/end/type, reads them back, and OKs the
// dialog only if the read-back verified — Cancels instead on any
// mismatch, never committing an unverified value.
//
// Returns true once the range list's item count has gone up by exactly
// one; false (logged) on any failure along the way: Add never opening the
// dialog, a missing control, a read-back mismatch, or a final count that
// didn't move by exactly one. Purely additive — never touches an existing
// range — and leaves `dialog` (Skip Setup itself) open either way, so a
// caller can batch several adds before its own final OK/Cancel.
bool AddFileSpecificSkipRange(const SkipSetupDialog& dialog, const core::SkipRange& range);

}
