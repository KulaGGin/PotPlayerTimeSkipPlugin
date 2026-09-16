#pragma once

#include "core/core.hpp"

// Live playback-state queries against PotPlayer's own main window (PTS-009).
// Backed by in-process SendMessage calls (docs/FINDINGS.md section 4) —
// synchronous and cheap, safe to call repeatedly from the hotkey/worker
// thread. None of these throw: the main window is re-resolved by class on
// every call where the cached handle has gone stale, and a window that
// still can't be found is logged once as a warning and reported as zero
// rather than raising into a thread with no reasonable way to react.
namespace plugin {

// PotPlayer's `0x5006` play-status reply. Only `1` (paused) has actually
// been observed live (docs/FINDINGS.md section 4); every other value is
// still meaningful, just not yet identified, so GetStatus() returns the raw
// reply rather than an enum that would have to guess at unobserved codes.
inline constexpr int kStatusPaused = 1;

core::Milliseconds GetPositionMs();
core::Milliseconds GetDurationMs();
int GetStatus();

// GetDurationMs() == 0 is how PotPlayer itself represents "no file open"
// (docs/FINDINGS.md section 4). Checking this rather than position lets
// callers tell that state apart from genuinely sitting at 0:00 in an open
// file, where duration is > 0.
bool IsFileOpen();

}
