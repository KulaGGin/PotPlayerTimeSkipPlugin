#pragma once

#include "core/config.hpp"

// Live half of PTS-016: turns core::ParseConfig's pure parsing into the
// actual on-disk %LOCALAPPDATA%\PotPlayerTimeSkip\config.ini — the same
// directory diagnostics/log.cpp already uses for plugin.log. Kept out of
// core/config.hpp so that header stays <windows.h>-free and unit-testable
// on its own; the live half (CreateFileW et al.) lives in config.cpp.
namespace plugin {

// Reads config.ini, parsing it via core::ParseConfig and logging every
// warning it reports (LOG_WARN) — a malformed hotkey spec, an unknown
// key/section, a line outside any section — then returns the resulting
// core::Config either way. Creates the file (core::DefaultConfig(),
// serialized) if it doesn't exist yet. A config directory/file that can't
// be created, read, or written is itself only logged (LOG_WARN), never
// fatal: this always returns a usable Config, falling all the way back to
// core::DefaultConfig() if the file can't be dealt with at all — per
// PTS-016's "never fail to load the plugin because of a bad config."
core::Config LoadConfig();

}
