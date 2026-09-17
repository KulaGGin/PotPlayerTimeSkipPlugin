#pragma once

#include <optional>
#include <string_view>

// The three hotkeys' static definitions and the pure press-id -> handler
// routing table (PTS-013). Kept <windows.h>-free like plugin/window.hpp, so
// the routing logic is unit-testable against synthetic ids with no live
// RegisterHotKey/message loop needed; the live half (RegisterHotKey,
// WM_HOTKEY pump) lives in plugin/hotkey_pump.hpp.
namespace plugin {

// Named handlers the three keys route to. What each handler actually does
// is PTS-014's job (explicitly out of scope here) — these names identify
// the physical key, not any presumed behavior, so this table doesn't have
// to guess at PTS-014's design.
enum class HotkeyAction {
    kAltA,
    kAltOpenBracket,
    kAltCloseBracket,
};

// One row of the single definition point the issue asks for ("keys are
// configurable later (PTS-016); here they can be constants but wired
// through a single definition point"): the modifier/virtual-key pair
// RegisterHotKey needs, the id it's registered under (also what WM_HOTKEY's
// wParam reports back), and which named handler it routes to.
// `modifiers`/`virtualKey` are plain unsigned ints mirroring MOD_ALT/VK_* so
// this header stays <windows.h>-free — plugin/hotkey_pump.cpp is what
// actually hands them to RegisterHotKey.
struct HotkeyDefinition {
    int id;
    unsigned int modifiers;
    unsigned int virtualKey;
    HotkeyAction action;
    std::string_view name;
};

// MOD_ALT (winuser.h) and the three keys' virtual-key codes, spelled
// numerically here rather than pulling in <windows.h> just for these
// constants. VK_A == 'A', VK_OEM_4 == '[', VK_OEM_6 == ']' on the US layout
// this project targets.
inline constexpr unsigned int kModAlt = 0x0001;
inline constexpr unsigned int kVkA = 0x41;
inline constexpr unsigned int kVkOpenBracket = 0xDB;
inline constexpr unsigned int kVkCloseBracket = 0xDD;

// Ids are arbitrary but must be nonzero and distinct — RegisterHotKey's own
// requirement, and what WM_HOTKEY's wParam echoes back for
// ResolveHotkeyAction below to look up.
inline constexpr HotkeyDefinition kHotkeyDefinitions[] = {
    {1, kModAlt, kVkA, HotkeyAction::kAltA, "Alt+A"},
    {2, kModAlt, kVkOpenBracket, HotkeyAction::kAltOpenBracket, "Alt+["},
    {3, kModAlt, kVkCloseBracket, HotkeyAction::kAltCloseBracket, "Alt+]"},
};

// Routes a WM_HOTKEY id (RegisterHotKey's own id, echoed back in wParam) to
// the action it was registered for. Returns nullopt for an id that isn't
// one of ours — shouldn't happen for a genuine WM_HOTKEY, but a stale or
// synthetic message shouldn't dispatch to a handler either way.
std::optional<HotkeyAction> ResolveHotkeyAction(int id);

}
