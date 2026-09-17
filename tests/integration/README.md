# tests/integration — PTS-018

An **opt-in** end-to-end suite that exercises the whole plugin against a
**live PotPlayer** and verifies the ground truth — the `.pbf` on disk —
because the parts that matter most here (does the real dialog automation
actually write what was asked, does the real hotkey gesture actually reach
it) can't be unit-tested. It mirrors the manual verification described in
[`docs/FINDINGS.md`](../../docs/FINDINGS.md) (apply ranges → read `.pbf`
back → compare millisecond-exact).

This is **not** part of `ctest --test-dir build` and never will be — it
needs a real PotPlayer window, a real proxy install, and a throwaway test
video, none of which the fast unit suite ([`tests/unit`](../unit)) assumes.
Run it explicitly, by hand, when you want to confirm the live Win32 driving
still works (after touching `plugin/src/skip_setup.cpp`,
`plugin/src/skip_marking.cpp`, or anything PTS-010/011/012/014 depend on;
also PTS-018's own acceptance gate for those issues in practice) — including
after a PotPlayer update, alongside
[`docs/REVERIFICATION_CHECKLIST.md`](../../docs/REVERIFICATION_CHECKLIST.md).

## Layout

- [`PbfCodec.psm1`](PbfCodec.psm1) — pure `.pbf` codec (build/parse/read),
  a PowerShell port of `core::BuildPlaySkipSection`/`ParsePlaySkipSection`.
  No PotPlayer needed.
- [`PbfCodec.Tests.ps1`](PbfCodec.Tests.ps1) — **fast, no-PotPlayer-needed**
  tests for the codec above, checked against `docs/FINDINGS.md`'s real
  captured examples. Run this on its own whenever `PbfCodec.psm1` changes;
  `Run-IntegrationTests.ps1`'s own millisecond-exact assertions are only as
  trustworthy as this codec is.
- [`Win32Interop.psm1`](Win32Interop.psm1) — the raw cross-process Win32
  plumbing (`PostMessage`/`SendMessage`/`WM_GETTEXT`/`LVM_SETITEMSTATE`/
  `SendInput`/...), the out-of-process counterpart to
  `plugin/src/skip_setup.cpp` and `plugin/src/player_window.cpp`.
- [`SkipSetupAutomation.psm1`](SkipSetupAutomation.psm1) — the high-level
  dialog driver built on the above: `Open-SkipSetupDialog`,
  `Add-SkipSetupRange`, `Clear-SkipSetupRanges`, `Set-SkipSetupEnabled`, etc.
  This is the "or calls the exported primitives" half of PTS-018 — the
  plugin has no literal exported DLL call for this, so this module
  re-derives the same dialog automation `docs/FINDINGS.md` section 3's own
  pre-PTS-010 cross-process prototype validated, from outside the process.
- [`PlayerQuery.psm1`](PlayerQuery.psm1) — cross-process playback position/
  duration queries (the `WM_USER` codes `docs/FINDINGS.md` section 4
  measured), used for preconditions and the hotkey test's before/after
  checks.
- [`HotkeyGesture.psm1`](HotkeyGesture.psm1) — sends real Alt+[ / Alt+] /
  Alt+A key chords via `SendInput`. This is "drives the real three-key
  gesture," reaching `plugin::RunHotkeyPump`'s `RegisterHotKey` exactly like
  a physical keypress would.
- [`Run-IntegrationTests.ps1`](Run-IntegrationTests.ps1) — the suite itself.

## Prerequisites

1. The proxy built and installed against a real PotPlayer:
   ```powershell
   cmake --build build --config Release
   powershell -File tools\install\potplayer-proxy.ps1 Install
   ```
2. PotPlayer running, with a **dedicated throwaway test video, at least 20
   seconds long,** open — never point this at a file from your real
   library. Every test case here adds/clears real skip ranges against
   whatever file is open. The suite always backs up and restores that
   file's `.pbf` (byte-exact, in a `try`/`finally`), but a PotPlayer crash
   mid-run would skip that restore, and the dialogs it drives are genuinely
   visible on screen (unlike the shipped plugin — see "What this does NOT
   hide" below). The 20-second minimum is enforced at startup (see "Known
   quirks" below for why it matters) — a synthetic clip works fine:
   ```powershell
   ffmpeg -f lavfi -i testsrc=duration=30:size=640x480:rate=30 -f lavfi -i sine=frequency=1000:duration=30 -c:v libx264 -c:a aac -shortest throwaway-clip.mp4
   ```

## Running it

```powershell
powershell -File tests\integration\Run-IntegrationTests.ps1 -TestVideoPath 'C:\path\to\throwaway-clip.mp4'
```

Add `-SkipHotkeyGestureTest` to skip the real-hotkey test case if you can't
get PotPlayer reliably foregrounded in your session (see below) — every
other test case drives the dialogs directly and never needs foreground at
all.

## What it covers

| Test case | Acceptance criterion |
|---|---|
| Add via Skip Setup with a known timestamp | `.pbf` matches millisecond-exact |
| A second Add tops up | additive behavior |
| Clear removes all ranges | `.pbf` file is deleted, not emptied (`docs/FINDINGS.md` section 1) |
| Enable-skip guard | checkbox turned off, then `Set-SkipSetupEnabled` turns it back on before the Add lands |
| The real three-key gesture | Alt+[ / Alt+] / Alt+A reaches the live hotkey pump and state machine end-to-end |

The first four drive Skip Setup/Skip Interval Setup directly
(`SkipSetupAutomation.psm1`) with **known, caller-chosen timestamps**, so
their `.pbf` assertions are genuinely millisecond-exact — each one adds *on
top of* whatever's already on the file (see "Known quirks" below for why),
so the assertions check that the specific range just added is present with
the right total count, not that the file's whole content is exactly one
thing. The last one drives the *real* hotkeys instead, which only ever
capture *whatever `plugin::GetPositionMs()` reports at the instant each key
is pressed* — this codebase has no supported way to seek playback to an
exact position (see `docs/FINDINGS.md` section 4's playback-query-only
`WM_USER` table), so that test only asserts the gesture's *shape* (a range
appeared, its start is before its end, the position moved between the two
marks) rather than an exact value. If your test clip is paused, position
won't move between Alt+[ and Alt+] and the test logs a no-op note instead
of failing.

## What this does NOT hide

The shipped plugin (PTS-010) opens Skip Setup with a `WH_CBT` hook that
parks it off-screen before its first paint — a same-process, same-thread
mechanism not available to an external script. `SkipSetupAutomation.psm1`
has no such trick: **the dialogs it drives will visibly flash on screen**
while this suite runs. That's fine for a test harness whose whole point is
observing ground truth, as opposed to the shipped feature's point of never
being seen. The "no focus stolen, nothing flashed" claim is only checked in
the one sense observable from outside the process — the hotkey-gesture test
asserts the foreground window is still PotPlayer's main window once the
commit settles — and only for that one test case, which goes through the
real (invisible) in-process path, not `SkipSetupAutomation.psm1`'s own
visible dialog driving.

## Foreground and the hotkey gesture test

`plugin::RunHotkeyPump`'s dispatch only acts on a hotkey press while
PotPlayer is the foreground app (`config.ini`'s `[Hotkeys] Global=false`
default — see `plugin/src/hotkey_pump.cpp`). The hotkey-gesture test
therefore needs PotPlayer foregrounded before it sends any key chord.
`Win32Interop.psm1`'s `Set-ForegroundWindowSafe` uses the standard
`AttachThreadInput` workaround for Windows' foreground-lock
(`docs/FINDINGS.md` section 6), but this can still fail depending on what
else is happening on your desktop. Two ways around it:

- Re-run with `-SkipHotkeyGestureTest` (every other test case is unaffected).
- Or set `[Hotkeys] Global=true` in `%LOCALAPPDATA%\PotPlayerTimeSkip\config.ini`
  and restart PotPlayer — the hotkeys then act regardless of foreground, so
  `Set-ForegroundWindowSafe` failing no longer matters for this test
  (though the foreground-unchanged assertion itself still needs PotPlayer
  foregrounded to be meaningful either way).

## Known quirks discovered while building this suite

Nothing in `docs/FINDINGS.md` anticipated these — they only surfaced from
actually driving a live session repeatedly. Recorded here so the next person
debugging a weird failure doesn't have to re-discover them from scratch.

**A range whose end exceeds the clip's duration can close the file
entirely.** Confirmed live: setting Skip Interval Setup's End field past the
clip's actual length made PotPlayer behave exactly as if the clip had
reached end-of-playback — even while paused — up to and including closing
the file. `Run-IntegrationTests.ps1` now refuses to run against a clip
under 20000ms for exactly this reason (its own built-in ranges top out at
17000ms, leaving a margin) — if you change those ranges, keep them well
inside your test clip's duration, and prefer a clip at least half a minute
long.

**Adding the first-ever range to a file with no prior `.pbf` does not
persist when driven by this suite's own out-of-process automation** —
confirmed repeatedly (including waiting 20+ seconds and even closing
PotPlayer entirely afterward) — even though the *identical* operation via
real physical hotkeys persists immediately. Every other transition (adding
to an already non-empty list, clearing one down to empty) persists
reliably via automation; it's specifically the 0→1 transition, and
specifically only when driven cross-process. `Run-IntegrationTests.ps1`
works around this automatically: if the test video has no `.pbf` yet, it
attempts a one-time bootstrap Add and fails fast with actionable guidance
(add one range by hand, then re-run) if that bootstrap itself doesn't
persist. Every scenario after that point adds *on top of* whatever's
already there rather than assuming a clean empty list, and `Clear` runs
last, once, with nothing added afterward.

**Even ordinary Add-then-OK cycles occasionally don't land, for reasons not
fully root-caused.** Rare, and not tied to any specific scenario — just a
residual flakiness in driving Skip Setup this fast and this mechanically,
that a human's own click-then-click pacing apparently never triggers. Every
scenario that adds a range is wrapped in a bounded retry (`Test-Case
-Retries 2`): a `[RETRY]` line in the output means this happened and
recovered, not that anything is actually broken. If you see it happen on
every single run, that's worth investigating further; an occasional retry
is expected.

**`SendInput`-synthesized key chords do not reliably reach
`RegisterHotKey`'s global dispatch on every machine**, even though the
identical *physical* key combo does (confirmed by hand: Alt+[/Alt+]/Alt+A
worked instantly when pressed for real, produced zero log lines when sent
via `SendInput` from this suite, with `[Hotkeys] Global=true` set to rule
out the foreground gate, and with matching, no-elevation-mismatch process
integrity levels ruled out too). This is a known-hard class of Windows
automation problem, not something this suite fully solves — if
`-SkipHotkeyGestureTest` isn't passed and the hotkey test can't get its
`SendInput` calls recognized, it'll likely time out waiting for a range
that never appears. Default to `-SkipHotkeyGestureTest` unless you've
independently confirmed `SendInput` reaches global hotkeys on your machine.

**PotPlayer resumes playback from wherever it last stopped**, so a short
test clip you've already watched through once may sit at (or very near)
its own end the next time you open it, making `Test-PotPlayerFileOpen`'s
duration check pass while the clip has effectively no room left to play —
reopen it (or seek back with Home/scrub to the start) before re-running if
a run fails with `PotPlayer reports no file open`.

## Cleanup guarantee

`Run-IntegrationTests.ps1` reads the test video's `.pbf` as raw bytes (or
records its absence) before running anything, and restores those exact
bytes — or deletes the file again if none existed — in a `finally` block
that runs whether the suite passed, failed, or threw. A PotPlayer crash or
a `Ctrl+C` mid-run is the one thing that can skip that restore; since this
script keeps no on-disk backup of its own, take one yourself first
(`Copy-Item`) if the test file's existing `.pbf` state actually matters —
or just use a fresh throwaway clip with no `.pbf` at all, as recommended
above.
