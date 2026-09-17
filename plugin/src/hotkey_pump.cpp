#include "plugin/hotkey_pump.hpp"

#include <iterator>

#include "diagnostics/log.hpp"
#include "plugin/hotkeys.hpp"
#include "plugin/skip_marking.hpp"

namespace plugin {

namespace {

// Routes a resolved press straight into the PTS-014 state machine. Kept
// fast/non-blocking here: the dialog-driving work only happens once a
// range actually completes (TryCommit), and even then it runs inline on
// this same pump thread — a rapid Alt+[ then Alt+] between two different
// entries can't interleave since WM_HOTKEY messages are handled one at a
// time in press order, and a single entry's own two presses are exactly
// what triggers that one dialog round trip.
void Invoke(SkipMarkingStateMachine& machine, HotkeyAction action) {
    switch (action) {
    case HotkeyAction::kAltA:
        machine.OnAltA();
        return;
    case HotkeyAction::kAltOpenBracket:
        machine.OnAltOpenBracket();
        return;
    case HotkeyAction::kAltCloseBracket:
        machine.OnAltCloseBracket();
        return;
    }
}

// PTS-013's default intent: keys act only while PotPlayer is the app you're
// actually watching. Checked by owning process rather than by matching a
// specific cached HWND (plugin/player_window.cpp's main-window handle) —
// the proxy only ever loads inside PotPlayer's own process, so this also
// covers fullscreen playback or any other of PotPlayer's own top-level
// windows (e.g. a dialog) having focus, not just the main window itself.
bool IsPotPlayerForeground() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD ownerProcessId = 0;
    GetWindowThreadProcessId(foreground, &ownerProcessId);
    return ownerProcessId == GetCurrentProcessId();
}

// Tracks which of kHotkeyDefinitions' ids actually registered, so teardown
// only ever unregisters what registration actually succeeded on.
struct RegisteredHotkeys {
    bool ok[std::size(kHotkeyDefinitions)]{};
};

RegisteredHotkeys RegisterHotkeys() {
    RegisteredHotkeys registered;
    for (std::size_t i = 0; i < std::size(kHotkeyDefinitions); ++i) {
        const auto& definition = kHotkeyDefinitions[i];
        // MOD_NOREPEAT so holding a key down doesn't flood WM_HOTKEY —
        // PTS-013 asks for presses to route, not autorepeat spam.
        if (RegisterHotKey(nullptr, definition.id, definition.modifiers | MOD_NOREPEAT,
                            definition.virtualKey)) {
            registered.ok[i] = true;
            continue;
        }
        // Not fatal: a hotkey already taken by another app is logged and
        // skipped, per PTS-013's "the plugin stays alive" requirement —
        // the other two keys still work.
        LOG_WARN("hotkeys: RegisterHotKey failed for {}, gle={}", definition.name, GetLastError());
    }
    return registered;
}

void UnregisterHotkeys(const RegisteredHotkeys& registered) {
    for (std::size_t i = 0; i < std::size(kHotkeyDefinitions); ++i) {
        if (!registered.ok[i]) {
            continue;
        }
        if (!UnregisterHotKey(nullptr, kHotkeyDefinitions[i].id)) {
            LOG_WARN("hotkeys: UnregisterHotKey failed for {}, gle={}", kHotkeyDefinitions[i].name,
                      GetLastError());
        }
    }
}

// Drains every message currently queued (never just one), so a burst of
// rapid presses is fully processed before the loop goes back to waiting —
// what actually satisfies PTS-013's "rapid successive presses are not
// silently dropped" for today's fast/non-blocking handlers above.
void DrainMessageQueue(SkipMarkingStateMachine& machine) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_HOTKEY) {
            const auto action = ResolveHotkeyAction(static_cast<int>(msg.wParam));
            if (action && IsPotPlayerForeground()) {
                Invoke(machine, *action);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

void RunHotkeyPump(HANDLE stopEvent) {
    const RegisteredHotkeys registered = RegisterHotkeys();
    SkipMarkingStateMachine machine(MakeLiveSkipMarkingDriver());
    LOG_INFO("hotkeys: pump started");

    for (;;) {
        const DWORD wait = MsgWaitForMultipleObjects(1, &stopEvent, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0) {
            // stopEvent signaled: stop pumping and tear down below, even if
            // messages are still queued — nothing dispatches after this.
            break;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            // Some other wait failure (WAIT_FAILED) — nothing to recover
            // into; stop pumping rather than spin.
            LOG_ERROR("hotkeys: MsgWaitForMultipleObjects failed, gle={}", GetLastError());
            break;
        }
        DrainMessageQueue(machine);
    }

    LOG_INFO("hotkeys: pump stopping");
    UnregisterHotkeys(registered);
}

}  // namespace plugin
