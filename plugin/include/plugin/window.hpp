#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Pure window-selection logic (PTS-009), kept free of <windows.h> so it
// stays unit-testable with synthetic data, no live player needed. The
// live-window-facing half (EnumWindows, SendMessage) lives in
// plugin/player_window.hpp and calls into SelectWindow below.
namespace plugin {

// One top-level window as seen by an EnumWindows pass already filtered to
// the current process (matching PID is the caller's job, not this file's).
// `handle` is an opaque HWND value narrowed to an integer so this header
// never has to name the Win32 type. `title` defaults empty for callers (like
// the main-window search) that only ever care about class.
struct WindowCandidate {
    std::uintptr_t handle;
    std::wstring className;
    bool visible;
    std::wstring title{};
};

// Picks which candidate to treat as *the* window of class `wantedClassName`.
// Returns the first visible match, since PotPlayer only ever creates one
// live `PotPlayer64` window and a visible one is what queries should target;
// falls back to the first match at all (visible or not) so a window that
// merely hasn't been shown yet is still found rather than reported missing.
// Returns nullopt if nothing matches.
//
// Deliberately generic rather than hardcoded to the main window: PTS-010
// reuses this same predicate to find the Skip Setup dialog by its own class.
std::optional<std::uintptr_t> SelectWindow(const std::vector<WindowCandidate>& candidates,
                                            std::wstring_view wantedClassName);

// Picks the candidate matching both class and title exactly. Needed for
// PotPlayer's dialogs, which share a window class (`#32770`) across several
// distinct dialogs (Skip Setup, Skip Interval Setup, ...) — class alone
// can't tell them apart. Unlike SelectWindow, there is no visible/invisible
// fallback tier: PotPlayer only ever has one dialog with a given title open
// at a time, so the first exact match (if any) is it. Returns nullopt if
// nothing matches.
std::optional<std::uintptr_t> SelectWindowByTitle(const std::vector<WindowCandidate>& candidates,
                                                   std::wstring_view wantedClassName,
                                                   std::wstring_view wantedTitle);

}
