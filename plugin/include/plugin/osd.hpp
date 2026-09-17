#pragma once

#include <optional>
#include <string>

#include "core/core.hpp"

// PTS-015: on-screen feedback for the PTS-013/014 hotkey workflow. Message
// composition (event + times -> display text) is kept <windows.h>-free like
// plugin/skip_marking.hpp and plugin/window.hpp so it's table-tested with no
// live player or window anywhere in the test binary. ShowOsdLive (osd.cpp)
// is the only place this feature touches a real window — no native
// PotPlayer OSD channel for arbitrary text was found (docs/FINDINGS.md
// section 9: a resource-string scan of PotPlayer64.dll turned up only a
// "Message (OSD) settings" config-dialog entry, no command id), so this
// ships the issue's fallback: our own transient overlay.
namespace plugin {

enum class OsdEvent {
    kNoFileOpen,     // a hotkey was pressed with no file open
    kNewMark,        // Alt+A started a fresh entry
    kMarkIgnored,    // Alt+[ or Alt+] would have made a backwards/empty range
    kMarkStart,      // Alt+[ set the active entry's start (no commit yet)
    kMarkEnd,        // Alt+] set the active entry's end (no commit yet)
    kSaved,          // both bounds were set and commitRange succeeded
    kSaveFailed,     // both bounds were set but commitRange failed
};

// Not every event needs both times: kMarkStart reads only startMs, kMarkEnd
// only endMs, kSaved both, and the rest neither. ComposeOsdText only reads
// the field(s) its event actually needs — the caller is trusted to supply
// them, the same internal-invariant trust plugin/skip_marking.cpp already
// places in its own callers.
struct OsdMessage {
    OsdEvent event;
    std::optional<core::Milliseconds> startMs{};
    std::optional<core::Milliseconds> endMs{};
};

std::string ComposeOsdText(const OsdMessage& message);

// Shows `text` as a brief, auto-dismissing, click-through, non-activating
// toast positioned over PotPlayer's own main window, then tears itself down
// with no further action needed from the caller. A no-op (logged) if the
// main window can't currently be found. Must be called from a thread that
// pumps its own message queue — PTS-013's hotkey pump thread — since the
// toast's own WM_PAINT and auto-dismiss WM_TIMER are delivered there, the
// same way plugin/skip_setup.hpp's dialog-driving relies on that thread's
// loop to deliver HCBT/WM_COMMAND traffic.
void ShowOsdLive(const std::string& text);

// PTS-016: config-driven overrides applied to every future ShowOsdLive call.
// SetOsdEnabled(false) makes ShowOsdLive a silent no-op — the feature is
// "off", not a failure, so nothing is logged. Both are only ever touched
// from PTS-013's single hotkey pump thread before it starts registering
// hotkeys, the same "no lock needed" contract g_activeToast already relies
// on, so a plain atomic store/load is enough.
void SetOsdEnabled(bool enabled);
void SetOsdDurationMs(unsigned int durationMs);

}
