# PotPlayer Time Skip Plugin

A native x64 plugin for PotPlayer (PotPlayerMini64) that lets you mark and
auto-skip ranges of a video (intros, recaps, ads, ...) on future playback.

The repository is still at the scaffolding stage — see the
[issue backlog](https://github.com/KulaGGin/PotPlayerTimeSkipPlugin/issues)
for the full plan. Nothing here talks to PotPlayer yet.

## Layout

- `core/` — timecode / skip-range logic. Static library, no Win32
  dependency, unit-testable on its own.
- `diagnostics/` — the plugin's always-available diagnostic log (`LOG_INFO`
  / `LOG_WARN` / `LOG_ERROR`), safe to call from a `DllMain`-like context.
  Static library, links into `plugin` and `proxy`. Writes UTF-8 lines to
  `%LOCALAPPDATA%\PotPlayerTimeSkip\plugin.log` and mirrors them to
  `OutputDebugString`.
- `plugin/` — the in-process plugin. Links `core` and `diagnostics`.
- `proxy/` — `MediaDB64.dll`, the pass-through proxy PotPlayer loads in
  place of its own (stub for now). Links `diagnostics`.
- `tests/` — `unit_tests` (Catch2, via CTest); see [`tests/README.md`](tests/README.md)
  for how the harness works and how to add tests.
- `third_party/` — vendored dependencies (currently just Catch2).
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
