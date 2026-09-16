#include "plugin/skip_setup.hpp"

#include <windows.h>

#include <atomic>
#include <cwchar>
#include <iterator>

#include "diagnostics/log.hpp"
#include "plugin/player_window.hpp"
#include "plugin/window.hpp"
#include "plugin/window_enum.hpp"

namespace plugin {

namespace {

constexpr wchar_t kSkipSetupTitle[] = L"Skip Setup";
constexpr wchar_t kShadowClass[] = L"PotShadowWnd";

// Polling budgets: the open side just waits on the hook below to fill in
// g_capturedDialog/g_capturedShadow, the close side waits for OK/Cancel's
// handler to actually destroy the window.
constexpr DWORD kPollIntervalMs = 2;
constexpr DWORD kOpenTimeoutMs = 1000;
// How much longer to wait for the shadow after the dialog itself has already
// been captured — it's a side effect of the dialog appearing, created
// within the same burst of window-creation traffic, not a separate command.
constexpr DWORD kShadowGraceMs = 100;
constexpr DWORD kCloseTimeoutMs = 2000;

// Set only while OpenSkipSetup() is actively waiting on a just-posted open
// command, so g_hookProc never reacts to unrelated window traffic on
// PotPlayer's UI thread the rest of the time. Plain globals, not per-call
// state: PTS-010's whole design assumes one open/close cycle in flight at a
// time (see plugin/skip_setup.hpp), same as player_window.cpp's cached
// g_mainWindow.
std::atomic<bool> g_hookArmed{false};
std::atomic<HWND> g_capturedDialog{nullptr};
std::atomic<HWND> g_capturedShadow{nullptr};
HHOOK g_hook = nullptr;

bool ClassNameEquals(LPCWSTR lpszClass, const wchar_t* want) {
    // A predefined/system class (e.g. the dialog manager's own "#32770") is
    // handed to CBT hooks as an atom, not a string — never a match here, but
    // never worth dereferencing as a string either.
    if (IS_INTRESOURCE(lpszClass)) {
        return false;
    }
    return _wcsicmp(lpszClass, want) == 0;
}

// Real class/title, read off the live window rather than the CREATESTRUCT:
// measured live (PTS-010) that Skip Setup's CREATESTRUCT.lpszName is *not*
// yet "Skip Setup" at HCBT_CREATEWND time (presumably set later, e.g. via an
// explicit SetWindowText during its own WM_INITDIALOG) — matching against it
// there missed the dialog entirely, leaving a real, fully visible, untouched
// dialog with nothing waiting to close it. HCBT_ACTIVATE fires later, with a
// real HWND whose title is already whatever it's going to be, and still
// before the system actually shows/paints the window.
bool IsSkipSetupDialog(HWND hwnd) {
    wchar_t className[16];
    if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) <= 0) {
        return false;
    }
    if (wcscmp(className, L"#32770") != 0) {
        return false;
    }
    wchar_t title[64];
    const int titleLength = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    return titleLength > 0 && wcscmp(title, kSkipSetupTitle) == 0;
}

void ParkOffScreen(HWND hwnd) {
    SetWindowPos(hwnd, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// A WH_CBT hook, not a poll loop, is what actually kills the visible flash
// (measured live, PTS-010 — see docs/FINDINGS.md section 7): a "find the
// window, then SetWindowPos it off-screen" loop measured a real on-screen
// flash, because by the time the dialog was found it had already been
// painted once at its default (owner-centered) position.
//
// The shadow (PotShadowWnd, an app-defined class) is caught at
// HCBT_CREATEWND — its CREATESTRUCT already carries the real class name — by
// rewriting its position before CreateWindowEx ever applies it, so it's
// never at an on-screen position, not even for one frame.
//
// The dialog itself needs HCBT_ACTIVATE instead (see IsSkipSetupDialog
// above for why): it fires with a real, fully-initialized HWND, still
// before the system shows/paints it, letting SetWindowPos move it off-screen
// just in time. It would never work for the shadow — WS_EX_NOACTIVATE
// popups like a drop shadow never get an HCBT_ACTIVATE at all.
LRESULT CALLBACK CbtHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (g_hookArmed.load(std::memory_order_relaxed)) {
        if (code == HCBT_CREATEWND) {
            auto* createInfo = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
            CREATESTRUCTW* cs = createInfo->lpcs;
            if (ClassNameEquals(cs->lpszClass, kShadowClass)) {
                cs->x = -32000;
                cs->y = -32000;
                g_capturedShadow.store(reinterpret_cast<HWND>(wParam), std::memory_order_relaxed);
            }
        } else if (code == HCBT_ACTIVATE) {
            const HWND hwnd = reinterpret_cast<HWND>(wParam);
            if (IsSkipSetupDialog(hwnd)) {
                ParkOffScreen(hwnd);
                g_capturedDialog.store(hwnd, std::memory_order_relaxed);
            }
        }
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

struct CapturedWindows {
    HWND dialog;
    HWND shadow;
};

// Fallback for the case the hook still doesn't catch (matching PTS-010's
// acceptance criteria: never leave a mismatch/miss as silent wrong
// behavior). Late and poll-based, so it can't promise "no flash," but it
// guarantees OpenSkipSetup() never again leaves a real dialog sitting open
// with nothing watching it — the actual failure measured live before this
// fallback existed.
HWND FindAndParkFallback() {
    constexpr wchar_t kDialogClass[] = L"#32770";
    const ULONGLONG deadline = GetTickCount64() + kOpenTimeoutMs;
    do {
        const auto enumerated = EnumerateOwnProcessWindows();
        const auto selected = SelectWindowByTitle(enumerated.candidates, kDialogClass, kSkipSetupTitle);
        if (selected) {
            const HWND dialog = enumerated.handlesByValue.at(*selected);
            ParkOffScreen(dialog);
            return dialog;
        }
        Sleep(kPollIntervalMs);
    } while (GetTickCount64() < deadline);
    return nullptr;
}

// Arms the hook, posts the open command, and waits for it to capture the
// dialog (and, briefly, its shadow), falling back to a late poll-and-park if
// the hook alone doesn't catch the dialog. Always disarms and unhooks
// before returning, success or not — the hook must never outlive a single
// open attempt.
std::optional<CapturedWindows> OpenViaHook(HWND mainWindow) {
    const DWORD mainThreadId = GetWindowThreadProcessId(mainWindow, nullptr);

    g_capturedDialog.store(nullptr, std::memory_order_relaxed);
    g_capturedShadow.store(nullptr, std::memory_order_relaxed);
    g_hookArmed.store(true, std::memory_order_relaxed);

    // hMod is NULL because the hook procedure lives in this same process as
    // the target thread (we're a DLL loaded inside PotPlayer itself) — no
    // cross-process injection involved.
    g_hook = SetWindowsHookExW(WH_CBT, &CbtHookProc, nullptr, mainThreadId);
    if (g_hook == nullptr) {
        LOG_ERROR("plugin: SetWindowsHookEx(WH_CBT) failed, gle={}", GetLastError());
        g_hookArmed.store(false, std::memory_order_relaxed);
        return std::nullopt;
    }

    // PostMessage, never SendMessage: PotPlayer's own handler for this
    // command runs a modal DialogBox loop that won't return until Skip
    // Setup closes, which would otherwise block this thread for as long as
    // the dialog stays open (docs/FINDINGS.md section 6).
    if (!PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(kOpenSkipSetupCommandId, 0), 0)) {
        LOG_ERROR("plugin: PostMessage(open Skip Setup) failed, gle={}", GetLastError());
        g_hookArmed.store(false, std::memory_order_relaxed);
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
        return std::nullopt;
    }

    HWND dialog = nullptr;
    const ULONGLONG openDeadline = GetTickCount64() + kOpenTimeoutMs;
    do {
        dialog = g_capturedDialog.load(std::memory_order_relaxed);
        if (dialog != nullptr) {
            break;
        }
        Sleep(kPollIntervalMs);
    } while (GetTickCount64() < openDeadline);

    if (dialog == nullptr) {
        LOG_WARN("plugin: Skip Setup hook capture missed, falling back to poll-and-park");
        dialog = FindAndParkFallback();
    }

    HWND shadow = nullptr;
    if (dialog != nullptr) {
        const ULONGLONG shadowDeadline = GetTickCount64() + kShadowGraceMs;
        do {
            shadow = g_capturedShadow.load(std::memory_order_relaxed);
            if (shadow != nullptr) {
                break;
            }
            Sleep(kPollIntervalMs);
        } while (GetTickCount64() < shadowDeadline);
    }

    g_hookArmed.store(false, std::memory_order_relaxed);
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;

    if (dialog == nullptr) {
        return std::nullopt;
    }
    return CapturedWindows{dialog, shadow};
}

bool WaitForWindowGone(HWND hwnd) {
    const ULONGLONG deadline = GetTickCount64() + kCloseTimeoutMs;
    for (;;) {
        if (!IsWindow(hwnd)) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        Sleep(kPollIntervalMs);
    }
}

bool ClickAndWaitClosed(const SkipSetupDialog& dialog, int buttonId, std::uintptr_t buttonHandle) {
    const HWND dlg = reinterpret_cast<HWND>(dialog.hwnd);
    const HWND button = reinterpret_cast<HWND>(buttonHandle);

    if (!PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(buttonId, BN_CLICKED), reinterpret_cast<LPARAM>(button))) {
        LOG_ERROR("plugin: PostMessage(Skip Setup button {}) failed, gle={}", buttonId, GetLastError());
        return false;
    }
    if (!WaitForWindowGone(dlg)) {
        LOG_ERROR("plugin: Skip Setup did not close within {} ms", kCloseTimeoutMs);
        return false;
    }
    return true;
}

}  // namespace

std::variant<SkipSetupControls, MissingSkipSetupControl> ResolveSkipSetupControls(
    const std::function<std::uintptr_t(int)>& lookup) {
    struct Slot {
        int id;
        std::uintptr_t SkipSetupControls::*member;
    };
    const Slot slots[] = {
        {kEnableCheckboxId, &SkipSetupControls::enableCheckbox},
        {kRangeListId, &SkipSetupControls::rangeList},
        {kAddButtonId, &SkipSetupControls::addButton},
        {kEditButtonId, &SkipSetupControls::editButton},
        {kDeleteButtonId, &SkipSetupControls::deleteButton},
        {kOkButtonId, &SkipSetupControls::okButton},
        {kCancelButtonId, &SkipSetupControls::cancelButton},
    };

    SkipSetupControls controls{};
    for (const auto& slot : slots) {
        const std::uintptr_t handle = lookup(slot.id);
        if (handle == 0) {
            return MissingSkipSetupControl{slot.id};
        }
        controls.*(slot.member) = handle;
    }
    return controls;
}

std::optional<SkipSetupDialog> OpenSkipSetup() {
    const HWND mainWindow = reinterpret_cast<HWND>(GetMainWindowHandle());
    if (mainWindow == nullptr) {
        LOG_WARN("plugin: cannot open Skip Setup, main window not found");
        return std::nullopt;
    }

    const auto captured = OpenViaHook(mainWindow);
    if (!captured) {
        LOG_ERROR("plugin: Skip Setup did not appear within {} ms", kOpenTimeoutMs);
        return std::nullopt;
    }
    const HWND dialog = captured->dialog;

    auto resolution =
        ResolveSkipSetupControls([dialog](int id) { return reinterpret_cast<std::uintptr_t>(GetDlgItem(dialog, id)); });
    if (const auto* missing = std::get_if<MissingSkipSetupControl>(&resolution)) {
        LOG_ERROR("plugin: Skip Setup opened but control id {} is missing — layout mismatch?", missing->id);
        return std::nullopt;
    }

    return SkipSetupDialog{reinterpret_cast<std::uintptr_t>(dialog), reinterpret_cast<std::uintptr_t>(captured->shadow),
                            std::get<SkipSetupControls>(resolution)};
}

bool CloseSkipSetupOk(const SkipSetupDialog& dialog) {
    return ClickAndWaitClosed(dialog, kOkButtonId, dialog.controls.okButton);
}

bool CloseSkipSetupCancel(const SkipSetupDialog& dialog) {
    return ClickAndWaitClosed(dialog, kCancelButtonId, dialog.controls.cancelButton);
}

}  // namespace plugin
