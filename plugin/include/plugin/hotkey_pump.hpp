#pragma once

#include <windows.h>

// Live RegisterHotKey/WM_HOTKEY half of PTS-013, kept out of
// plugin/hotkeys.hpp so that header's routing table stays <windows.h>-free
// and unit-testable. Owns the worker thread's hotkey message pump for as
// long as the proxy DLL is loaded.
namespace plugin {

// Pumps the calling thread's message queue until `stopEvent` is signaled:
// loads PTS-016's config.ini first (plugin::LoadConfig), registers
// plugin::kHotkeyDefinitions' three actions under whatever modifiers/key
// the config resolved to (a registration failure — the key is already
// taken by another app — is logged and skipped, never fatal, per PTS-013's
// "the plugin stays alive" requirement), dispatches each WM_HOTKEY that
// arrives to its named handler via plugin::ResolveHotkeyAction, and
// unregisters whatever it managed to register before returning.
//
// Must be called from the thread that will own the hotkeys — Windows only
// ever posts WM_HOTKEY to the thread that registered it — and that thread
// has nothing else to pump messages for it, so this call blocks until
// `stopEvent` is signaled; it *is* the worker thread's message loop, not
// something run alongside one.
//
// Dispatch is gated on PotPlayer being the foreground app, checked on every
// press rather than once at registration: RegisterHotKey is unavoidably
// system-global (fires even when some other app is focused), so this
// per-press foreground check is what actually delivers PTS-013's "keys act
// only while you're watching" default intent, rather than a
// PotPlayer-scoped WH_KEYBOARD hook — more code and a second mechanism to
// maintain for the same outcome, given a message loop already has to exist
// here for WM_HOTKEY. PTS-016's config.ini can turn this check off
// (Hotkeys.Global=true), making the keys act everywhere.
void RunHotkeyPump(HANDLE stopEvent);

}
