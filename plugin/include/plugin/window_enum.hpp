#pragma once

#include <windows.h>

#include <unordered_map>

#include "plugin/window.hpp"

// Live EnumWindows-based discovery scoped to our own process (PTS-009,
// reused by PTS-010 for dialog discovery). Kept out of plugin/window.hpp,
// which stays <windows.h>-free and unit-testable; this is the live half that
// feeds it real candidates.
namespace plugin {

struct EnumeratedWindows {
    std::vector<WindowCandidate> candidates;
    // WindowCandidate.handle is narrowed to an integer (plugin/window.hpp
    // stays <windows.h>-free); this recovers the real HWND for whichever
    // candidate a caller picks.
    std::unordered_map<std::uintptr_t, HWND> handlesByValue;
};

// Enumerates every top-level window owned by the current process, with
// class name, title, and visibility populated on each candidate. Never
// includes another process's windows (matching PID is done internally).
EnumeratedWindows EnumerateOwnProcessWindows();

}
