# PotPlayer Time Skip Plugin

A native x64 plugin for PotPlayer (PotPlayerMini64) that lets you mark and
auto-skip ranges of a video (intros, recaps, ads, ...) on future playback.

The repository is still at the scaffolding stage — see the
[issue backlog](https://github.com/KulaGGin/PotPlayerTimeSkipPlugin/issues)
for the full plan. `proxy/` loads and bootstraps inside PotPlayer's own
process (see `docs/FINDINGS.md` §5); the actual skip-marking feature logic
(hotkeys, dialogs, OSD) isn't built yet.

## Layout

- `core/` — timecode / skip-range logic. Static library, no Win32
  dependency, unit-testable on its own.
- `diagnostics/` — the plugin's always-available diagnostic log (`LOG_INFO`
  / `LOG_WARN` / `LOG_ERROR`), safe to call from a `DllMain`-like context.
  Static library, links into `plugin` and `proxy`. Writes UTF-8 lines to
  `%LOCALAPPDATA%\PotPlayerTimeSkip\plugin.log` and mirrors them to
  `OutputDebugString`.
- `plugin/` — the in-process plugin. Links `core` and `diagnostics`. Provides
  window discovery and playback-state queries (`GetPositionMs`,
  `GetDurationMs`, `GetStatus`) against PotPlayer's own `PotPlayer64` window;
  opening/closing/driving the Skip Setup dialog off-screen with no focus
  steal (`OpenSkipSetup`, `AddFileSpecificSkipRange`, `DeleteSkipRange`,
  `ClearAllSkipRanges`, `EnsureSkipEnabled`); the `Alt+A`/`Alt+[`/`Alt+]`
  hotkey registration and dispatch (`RunHotkeyPump`); and the skip-marking
  state machine (`SkipMarkingStateMachine`) that ties those hotkeys to
  committing a range. OSD feedback lands in a later issue.
- `proxy/` — `MediaDB64.dll`, the proxy PotPlayer loads in place of its own,
  forwarding all three real exports to a renamed `MediaDB64_orig.dll` and
  bootstrapping `plugin` on a dedicated worker thread from `DllMain`. Links
  `plugin` and `diagnostics`.
- `tests/` — `unit_tests` (Catch2, via CTest); see [`tests/README.md`](tests/README.md)
  for how the harness works and how to add tests.
- `third_party/` — vendored dependencies (currently just Catch2).
- `tools/install/` — PowerShell install/uninstall/status/repair tooling for
  putting the proxy in and out of a real PotPlayer install; see
  [`tools/install/README.md`](tools/install/README.md).
- `docs/` — reference docs and research findings.

## Requirements

- Windows, x64.
- Visual Studio 2022+ (MSVC) or the standalone MSVC Build Tools.
- CMake 3.21+.

## Build

From a clean clone:

```
cmake -B build -A x64
cmake --build build --config Release
```

This produces, under `build/`:

- `proxy/Release/MediaDB64.dll` (+ `MediaDB64.pdb`)
- `tests/Release/unit_tests.exe`

## Test

```
ctest --test-dir build -C Release --output-on-failure
```

## Notes

- x64 only; there is no 32-bit configuration.
- `core/` must stay free of `<windows.h>` so it stays unit-testable on its
  own and never grows a dependency on the live player.
- Never commit real PotPlayer DLLs, renamed originals, registry snapshots,
  or personal PotPlayer install paths — see `.gitignore`.
