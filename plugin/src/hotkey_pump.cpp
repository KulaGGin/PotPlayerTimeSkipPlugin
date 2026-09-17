#include "plugin/hotkey_pump.hpp"

#include <iterator>

#include "diagnostics/log.hpp"
#include "plugin/config.hpp"
#include "plugin/hotkeys.hpp"
#include "plugin/osd.hpp"
#include "plugin/skip_marking.hpp"

namespace plugin {

namespace {

// Routes a resolved press straight into the PTS-014 state machine. Kept
// fast/non-blocking here: the dialog-driving work only happens on Alt+A
// (OnAltA), and even then it runs inline on this same pump thread — two
// rapid Alt+A commits can't interleave since WM_HOTKEY messages are handled
// one at a time in press order.
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

// PTS-016: which of config's three remappable specs backs a given static
// definition's action — the id/action/name pairing in kHotkeyDefinitions
// never changes, only the actual modifiers/virtualKey RegisterHotKey is
// called with.
core::HotkeySpec ConfiguredSpec(HotkeyAction action, const core::Config& config) {
    switch (action) {
    case HotkeyAction::kAltA:
        return config.newMarkHotkey;
    case HotkeyAction::kAltOpenBracket:
        return config.markStartHotkey;
    case HotkeyAction::kAltCloseBracket:
        return config.markEndHotkey;
    }
    return {};
}

RegisteredHotkeys RegisterHotkeys(const core::Config& config) {
    RegisteredHotkeys registered;
    for (std::size_t i = 0; i < std::size(kHotkeyDefinitions); ++i) {
        const auto& definition = kHotkeyDefinitions[i];
        const core::HotkeySpec spec = ConfiguredSpec(definition.action, config);
        // MOD_NOREPEAT so holding a key down doesn't flood WM_HOTKEY —
        // PTS-013 asks for presses to route, not autorepeat spam.
        if (RegisterHotKey(nullptr, definition.id, spec.modifiers | MOD_NOREPEAT, spec.virtualKey)) {
            registered.ok[i] = true;
            continue;
        }
        // Not fatal: a hotkey already taken by another app is logged and
        // skipped, per PTS-013's "the plugin stays alive" requirement —
        // the other two keys still work.
        LOG_WARN("hotkeys: RegisterHotKey failed for {} ({}), gle={}", definition.name,
                  core::FormatHotkeySpec(spec), GetLastError());
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
//
// `globalHotkeys` is PTS-016's config toggle: true skips the foreground
// check entirely (the keys act everywhere), false keeps PTS-013's original
// "only while you're watching" default.
void DrainMessageQueue(SkipMarkingStateMachine& machine, bool globalHotkeys) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_HOTKEY) {
            const auto action = ResolveHotkeyAction(static_cast<int>(msg.wParam));
            if (action && (globalHotkeys || IsPotPlayerForeground())) {
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
    // PTS-016: loaded once at startup, before anything else below so log
    // verbosity and OSD behavior are already in effect for every line this
    // function itself logs.
    const core::Config config = LoadConfig();
    diagnostics::SetMinSeverity(config.logVerbosity);
    SetOsdEnabled(config.osdEnabled);
    SetOsdDurationMs(static_cast<unsigned int>(config.osdDurationMs));

    const RegisteredHotkeys registered = RegisterHotkeys(config);
    SkipMarkingStateMachine machine(MakeLiveSkipMarkingDriver(config.autoEnableSkip));
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
        DrainMessageQueue(machine, config.globalHotkeys);
    }

    LOG_INFO("hotkeys: pump stopping");
    UnregisterHotkeys(registered);
}

}  // namespace plugin
