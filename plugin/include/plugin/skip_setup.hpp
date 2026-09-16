#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

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

}
