# FINDINGS.md — PotPlayer reverse-engineering results

This is the load-bearing document for the whole project: everything below was
**measured** against a running PotPlayer, not taken on faith from the
AutoHotkey community threads or assumed from how a "normal" media player
would work. Where a plausible guess turned out wrong, that is called out
explicitly — the project exists because an earlier design doc guessed skip
data lived in the registry (a `BMItem`/`BMList`-style layout) and that guess
was wrong. The discipline that produced the correction is: every claim below
is tagged **measured** (with the method — registry diff, window enumeration,
`dumpbin` import/export dump, or a live probe) or **inferred** (plausible,
but not independently confirmed).

## Provenance

- Build under test: `PotPlayerMini64.exe` dated **2025-04-22**, installed at
  `C:\Program Files\PotPlayer`. Main engine DLL `PotPlayer64.dll` is **~27
  MB**; the launcher `PotPlayerMini64.exe` itself is a thin **132 KB**
  wrapper around it.
- Everything here is scoped to that exact build. Control IDs, DLL export
  counts, and import tables are all properties of a specific binary — **a
  PotPlayer version bump invalidates every control-ID, offset, and
  export-count claim below until it is re-checked.** Re-verifying after an
  update is PTS-017's job (control-ID drift detection & version resilience);
  nothing here should be trusted blind against a different build.
- Source measurements: `tools/potplayer-skip-marker/FINDINGS.md` and
  `PotPlayerSkip.psm1` (the probing session's own findings doc and the
  PowerShell driver built to re-verify them end-to-end), plus a follow-up
  `dumpbin`-based loader survey done in the same line of research.

## 1. Storage: skip ranges live in a `.pbf` sidecar, not the registry

**Measured (registry diff):** the design doc's guess was a registry-based
list. A `PlaySkipList` subkey does exist under
`HKCU\Software\DAUM\PotPlayerMini64` — it is created lazily (absent in a
2026-09-01 baseline snapshot, present by 2026-09-16) — but it stayed
**empty** across a full Add → OK → OK round trip with Type = *File-specific*.
Nothing under any of the DAUM registry roots changed as a result of adding a
range.

**Measured (live probe):** file-specific skip ranges are instead written to
a **`.pbf` file next to the video**, the same sidecar PotPlayer uses for
bookmarks:

```
<video>.mp4  ->  <video>.pbf
```

It is written when **Skip Setup**'s OK is clicked, not when the Add
(Skip Interval Setup) dialog's OK is clicked.

**Measured (hex/encoding inspection):** UTF-16 LE with a BOM (`FF FE`), CRLF
line endings, a trailing blank line. Writing it as UTF-8 will not be read
back by PotPlayer.

Real captured example, a single range (Start `00:12:34.567`, Length
`671.111s`, Type = File-specific):

```ini
[PlaySkip]
0=1*754567*671111
1=
```

A second real captured example, 11 ranges added in one session and verified
byte-for-byte against the requested timecodes:

```ini
[PlaySkip]
0=1*41074*11111
1=1*477610*27027
...
10=1*1306906*365232
11=
```

**Measured:** the format is `<index>=<type>*<start_ms>*<length_ms>`:

- `<start_ms>` is the Start time in milliseconds (`754567` ms =
  `00:12:34.567`).
- `<length_ms>` is the **Length, not the end time** — field 3 is a duration,
  not `end_ms`. (`754567` start + `671111` length = `1425678` end.)
- The final `N=` line with an empty value is a **terminator**, present in
  pre-existing hand-made `.pbf` files too.

**Inferred, not confirmed:** the leading value (`1` in every capture above)
tracks the Type combo, where `1` = File-specific. We never confirmed what an
Overall-type entry writes, or where — writing one to find out risked
polluting the global list, and the feature only ever needs to emit `1`.

**Measured:** when the last entry is deleted through PotPlayer's own UI, the
whole `.pbf` file is deleted, not left empty.

## 2. The cache invariant: the `.pbf` is not re-read while the file is open

This is the single biggest risk the originating design doc flagged, and it
resolves **against** a "write the `.pbf` directly" design.

**Measured (live probe):** with a video open in PotPlayer, its on-disk
`.pbf` was rewritten externally, from one entry to two. Reopening Skip Setup
still reported `LVM_GETITEMCOUNT == 1` for the range list. PotPlayer reads
the `.pbf` once when the file is opened and holds the skip list in memory
from that point on; it writes the `.pbf` back out from that in-memory copy,
never re-reading it, for as long as the file stays open.

Consequence: an external edit to a `.pbf` while PotPlayer has that file open
is **invisible until the next reload**, and is **liable to be silently
clobbered** the moment PotPlayer itself next flushes its own in-memory copy
back to disk. This is why the project drives PotPlayer's own UI instead of
writing the sidecar file directly.

## 3. Dialog geography

**Measured (window enumeration + live driving):** both dialogs are plain
Win32 `#32770` dialogs with stable control IDs. A complete Add and a
complete Delete round trip were driven purely with
`PostMessage`/`SendMessage`/`WM_SETTEXT` from an out-of-process PowerShell
script — no synthesized keystrokes except the initial `Ctrl+\` used to open
Skip Setup.

### Skip Setup — `#32770`, title `Skip Setup`

**Superseded by PTS-010 (see section 7 below):** the cross-process prototype
used `Ctrl+\`, PotPlayer's own accelerator, because it required no
foreground-stealing prep beyond making PotPlayer the foreground window
first. In-process, that requirement disappears: PTS-010 posts the real
`WM_COMMAND` id (`10240`) straight to the main window instead, with no
synthesized keystroke and no foreground change of any kind.

| id | class | what |
|---|---|---|
| 3034 | Button | Enable skip feature (checkbox) |
| 3040 / 3088 | Button / Edit | Intro (at the start) |
| 3044 / 3095 | Button / Edit | Ending (at the end) |
| 3045 / 3069 | Button / Edit | Chapter title(s) |
| 3242 | SysListView32 | the range list |
| 3024 | Button | **Add...** |
| 3025 | Button | Edit... |
| 3026 | Button | Delete |
| 1 / 2 | Button | OK / Cancel |

### Skip Interval Setup — `#32770`, title `Skip Interval Setup`, opened via Skip Setup's Add... (id 3024)

| id | class | what |
|---|---|---|
| 3088 | Edit | Start time, `HH:MM:SS.mmm` |
| 3092 | Edit | End time, `HH:MM:SS.mmm` |
| 3091 | Edit | Length, seconds with 3 decimals |
| 3012 | ComboBox | Type — `[0]` Overall, `[1]` File-specific |
| 3034 | Button | Reverse playback time |
| 1 / 2 | Button | OK / Cancel |

**Measured:** both time edits default to the current playback position when
the dialog opens.

**Measured:** `WM_SETTEXT` into the Start/End edits fires the dialog's own
recalculation off the resulting `EN_CHANGE` — after setting `00:12:34.567` /
`00:23:45.678`, the Length field read back `671.111` and the summary static
read `00:12:34 ~ 00:23:45`, with no synthesized keystrokes needed.

**Measured:** `CB_SETCURSEL` on control 3012 sticks without a
`CBN_SELCHANGE` notification firing — the Type it is set to is the Type
that gets written, with no extra message needed.

## 4. Playback query interface

**Measured (window enumeration):** the main window class is **`PotPlayer64`**
— not `PotPlayerMini64`, the process name.

**Measured (live probe):** the AutoHotkey community's `WM_USER` (`0x0400`)
codes work on this build, read via
`SendMessage(hwnd, 0x0400, wParam, 0)`:

| wParam | returns |
|---|---|
| `0x5004` | current position, ms |
| `0x5002` | total duration, ms |
| `0x5006` | play status (`1` observed while paused; other values not yet observed) |

Cross-check: `0x5004` returned `1447`, matching the `00:00:01.447` the Add
(Skip Interval Setup) dialog had pre-filled from the live playback position
at the same instant.

## 5. Loader analysis

This determines what a drop-in proxy DLL could target, and is the direct
input to PTS-005 (proxy viability spike) and PTS-006 (the MediaDB64
forwarder DLL).

**Measured (`dumpbin` on `PotPlayerMini64.exe`'s import table):** the
launcher statically imports only **6 System32 DLLs, two functions each**:
`kernel32`, `user32`, `ole32`, and a `wintrust` / `crypt32` / `imagehlp`
trio. **Inferred:** that trio is a signature-verification check run at
startup — these three DLLs are the standard Authenticode-verification API
surface — but exactly what it verifies (the exe's own signature, or also
`PotPlayer64.dll`'s) was not independently confirmed.

**Measured:** `PotPlayerMini64.exe` does **not** statically import
`PotPlayer64.dll`. It loads the 27 MB engine DLL dynamically
(`LoadLibrary`) at startup instead — it is demonstrably loaded every
session despite being absent from the static import table.

**Measured (`dumpbin` on `PotPlayer64.dll`):** `PotPlayer64.dll` statically
imports 23 DLLs, and **none of them are app-local** — every DLL in the
PotPlayer install folder is loaded dynamically, by name, not through a
static import table. Load order is therefore: `.exe` →
(dynamically) `PotPlayer64.dll` → (dynamically) every app-folder DLL.

**Measured (KnownDLLs registry check, this machine):** `user32.dll` is
present in the `KnownDLLs` list. **Inferred (standard, well-documented
Windows loader behavior, not independently re-derived here):** a DLL on
that list is always loaded from `\System32` via the `\KnownDlls` object
directory before the loader ever looks in the application folder — so a
same-named DLL dropped into PotPlayer's install folder is silently ignored.
This rules out proxying `user32.dll` (or any other KnownDLL: kernel32,
gdi32, ole32, shell32, combase, ntdll, ...) by dropping a local copy next to
the executable.

**Measured (`dumpbin /exports` on every app-folder DLL):** ranked by how
many exports a forwarder DLL would need to re-export — fewer is a smaller,
easier surface to get exactly right:

| DLL | exports to forward | size | verdict |
|---|---|---|---|
| `MediaDB64.dll` | **3** | 3.9 MB | easiest by far |
| `ATextOut64.dll` | 33 | 4 MB | viable backup (FreeType/subtitle rendering) |
| `d3dcompiler_47.dll` | 29 | 4 MB | avoid — Microsoft redist, can load from System32/WinSxS instead |
| `PotPlayer64.dll` | 43 | 27 MB | avoid — it's the player itself, and the one most likely to be signature-checked |
| `ffcodec64.dll` | 202 | 41 MB | avoid — huge surface |
| `d3dx9_43.dll` | 329 | 2.3 MB | avoid — Microsoft redist, huge surface |

**Measured:** the `.ax` DirectShow splitters and `FFmpeg64.dll` live in a
`Module\` subfolder. **Inferred (not confirmed with a loader trace):** they
only load once a matching file actually plays, making them too conditional
for a proxy that needs to be resident from process start.

**Chosen proxy target: `MediaDB64.dll`, 3 exports** —
`CreateDatabaseEngine` (media library DB), `CreateJpegDecoder`
(thumbnails), `CreateSMTC` (Windows System Media Transport Controls, the
media-key overlay). Export names are measured (`dumpbin /exports`).
**Inferred, not yet confirmed against a freshly-started process:** those
three responsibilities (SMTC/media-key integration, history DB) are
expected to load at or near startup, before any file is opened — this is
inferred from what the exports *do*, not verified with a debugger or loader
trace against a cold start. Re-confirming this is in scope for PTS-005.

**Built and tested (PTS-005):** the proxy mechanism is to rename the real
file to `MediaDB64_orig.dll`, drop a replacement `MediaDB64.dll` in its
place exporting the same 3 names as PE forwarders
(`CreateDatabaseEngine=MediaDB64_orig.CreateDatabaseEngine`, etc., via MSVC
`#pragma comment(linker, "/EXPORT:...")` — a `.def`-file `EXPORTS
name=Dll.Func` line was tried first and failed to link as a forwarder under
this toolchain's linker; the `/EXPORT` pragma form worked) so the loader
resolves them transparently, and have that replacement's `DllMain` spin up
the plugin's own hotkey/dialog-driving thread.

### PTS-005 viability spike — verdict: **PASS, proceed with the proxy design**

**Measured (live swap test against `PotPlayerMini64.exe` 2025-04-22,
2026-09-16):** a pure pass-through forwarder (`proxy/`, no plugin logic,
`DllMain` only logs attach/detach) was built, the real `MediaDB64.dll` was
renamed to `MediaDB64_orig.dll`, the forwarder dropped in as `MediaDB64.dll`,
and PotPlayer relaunched.

- **The decisive result: no signature gate.** `PotPlayer64.dll` loaded the
  unsigned forwarder without rejection, crash, or degraded behavior. Proof
  it actually ran in-process, not just that the app didn't crash:
  `MediaDB64.dll` and `MediaDB64_orig.DLL` both showed up in the live
  process's module list (`Process.Modules`, both loaded — the forwarder
  RVAs pull in the original as soon as an export is resolved), and this
  proxy's own `DllMain` wrote `MediaDB64 proxy attached` to
  `%LOCALAPPDATA%\PotPlayerTimeSkip\plugin.log`, which only a DLL actually
  loaded and executing inside PotPlayer's process can produce.
- Process stayed responsive (`Process.Responding == true`) throughout, and
  the UI (main window, skin, playlist panel showing an opened test clip)
  rendered normally — a screenshot taken mid-test showed no degraded or
  fallback UI state.
- **Correction to the "near startup" assumption above:** `MediaDB64.dll` is
  **not** loaded at process launch. A cold launch reached a main window in
  ~700 ms with `MediaDB64.dll` absent from the module list at that point
  (confirmed by full module enumeration, 102 modules, none matching). It
  only loaded once a media file was actually opened/queued (a synthetic
  5-second test clip via `PlayerMini64.exe "<file>"`), appearing in the
  module list and logging attach within ~2-4 s of that call returning. The
  three exports' inferred responsibilities (library DB, thumbnails, SMTC)
  are apparently wired to first-file-open, not process start. This matters
  for PTS-006: the plugin's hotkey/dialog thread must be started from a
  trigger that survives this lazy load (`DllMain`'s `DLL_PROCESS_ATTACH`
  still fires exactly once at that later load point, so it remains a valid
  hook — it just fires later than originally assumed, not before).
- **Restore verified byte-exact:** after the test, `MediaDB64.dll` and
  `MediaDB64_orig.dll` were both stopped-process-then-swapped back
  (original moved back over the proxy), and the restored file's SHA-256
  matched the pre-test backup exactly. A second relaunch afterward showed
  normal unmodified behavior with only `MediaDB64.dll` present on disk (no
  leftover `_orig` file) and its original timestamp intact.

**Verdict:** proceed with the proxy-DLL design. The injector-launcher
fallback is not needed. PTS-006 builds the real plugin logic on top of this
forwarder, started from `DllMain`'s `DLL_PROCESS_ATTACH` at the (now
confirmed lazy, file-open-triggered) load point.

### PTS-006 — bootstrap threading: a real loader-lock deadlock, found and fixed

**Measured (via the `proxy_dllmain_no_deadlock` CTest case):** the first
bootstrap design — spawn a worker thread from `DLL_PROCESS_ATTACH`, and on
`DLL_PROCESS_DETACH` `SetEvent` it to stop and `WaitForSingleObject` on its
handle to join it — deadlocked. Every run of that CTest case took exactly
its 5-second `WaitForSingleObject` timeout (not the instant pass a working
join gives), and the diagnostic log confirmed why: the worker thread's own
*"started"* line never appeared before the join gave up. Root cause: a new
thread's own startup (`CreateThread`) must acquire the loader lock to run
`DLL_THREAD_ATTACH` notifications for every other loaded module before it
reaches any user code — but the thread calling `WaitForSingleObject` to join
it is doing so from inside `DllMain`, which already holds that same loader
lock and won't release it until the join returns. Self-deadlock, silently
"resolved" only by the join's own timeout — and even then, the DLL would be
unmapped by `FreeLibrary` while that worker thread might still be stuck
mid-startup inside it.

**Fix:** never join the worker thread from `DllMain` at all. Instead, the
proxy takes an extra reference to its own module in `DLL_PROCESS_ATTACH` via
`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)` (a safe
refcount bump — no `DllMain` re-entry, unlike calling `LoadLibrary` from
`DllMain`, which is itself unsafe). `DLL_PROCESS_DETACH` just signals a stop
event and returns immediately, no wait. The worker thread, once it wakes on
that event, calls `FreeLibraryAndExitThread` — which releases the extra
reference and exits the thread as one atomic step, so the module is only
ever unmapped after that thread has already left it. This is the standard
pattern for "a thread that must free its own DLL and stop." After the fix,
the same CTest case passes in ~0.01 s instead of hanging out the full 5 s
timeout.

**Measured (live test against real PotPlayer, same swap/restore cycle as
PTS-005):** with the fixed proxy installed and a test clip opened, the log
showed the full expected sequence — `MediaDB64 proxy attached` on the
loader's own thread, then, on a **different** thread ID, `proxy worker
thread started` followed by `plugin::placeholder called` — confirming the
bootstrap genuinely runs off the loader-lock-holding thread, inside the
real process, without stalling it. The main window and playlist rendered
normally (screenshot-verified) and the process stayed responsive throughout.
A force-kill (`TerminateProcess`, the worst case — no `DllMain` notification
at all) exited cleanly with no hang or leftover process. The original DLL
was restored and hash-verified byte-identical afterward, and a final
relaunch confirmed normal unmodified behavior.

**Ordinals:** the real `MediaDB64.dll`'s export ordinals (`dumpbin
/exports`) are 1/2/3 for `CreateDatabaseEngine`/`CreateJpegDecoder`/
`CreateSMTC` respectively — the proxy's `/EXPORT` forwarders now pin the
same ordinals explicitly (`,@1` / `,@2` / `,@3`) rather than relying on the
linker's default assignment happening to match.

## 6. Cross-process gotchas

Kept specifically for the injector-launcher fallback and for any future
out-of-process tooling — none of this applies once the plugin runs
in-process, but it cost real debugging time to find.

**Measured:** `SendMessage(..., BM_CLICK, ...)` on the Skip Setup "Add..."
button never returns. The Add (Skip Interval Setup) dialog is modal, so its
message loop runs *inside* the `SendMessage` call and the caller blocks
until that dialog is closed by someone else. Use
`PostMessage(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), hCtl)` instead,
which returns immediately.

**Measured:** `GetWindowText` returns an empty string for a control owned
by another process. Only `WM_GETTEXT` (sent via `SendMessage`) is marshalled
across the process boundary and actually returns the control's text.

**Measured (found via a live activation failure):** `SetForegroundWindow`
from a background process is blocked by Windows' foreground-lock unless the
caller's input queue is attached (`AttachThreadInput`) to the thread that
currently owns the foreground window — attaching only to the *target*
window's thread is not sufficient and fails intermittently depending on
what the user happens to have focused.

**Measured:** PotPlayer draws each dialog's drop shadow in a separate
`PotShadowWnd` top-level window. Moving a dialog off-screen (to drive it
without it being visible) must move its `PotShadowWnd` too, or a shadow
rectangle is left floating over the video with nothing inside it.

## 7. Opening Skip Setup in-process (PTS-010)

**Measured (static resource analysis, no live PotPlayer run needed):**
PotPlayer's `WM_COMMAND` id for opening Skip Setup is **`10240`**. Found by
loading `PotPlayer64.dll` with `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)`
(no code from the DLL ever executes) and walking its `RT_MENU` resource with
`LoadMenu`/`GetSubMenu`/`GetMenuItemID`/`GetMenuStringW`: the entry under
Play > Playback Skip reads "재생 스킵 설정..." (this build's UI is Korean, not
English) with id `10240`, and the same id shows up in the `RT_ACCELERATOR`
table (`LoadAccelerators`/`CopyAcceleratorTable`) bound to the bare `'` key —
not `Ctrl+\`, which was the cross-process prototype's own accelerator choice,
not a property of this command id. Posting
`WM_COMMAND` with this id directly to the main window (`PostMessage`, never
`SendMessage` — the same modal-dialog-blocks-the-caller hazard as section 6's
Add... button gotcha, since PotPlayer's handler for this id runs its own
`DialogBox` loop) opens Skip Setup with no synthesized keystroke and no
foreground-window change at all.

**Measured (live, via a temporary probe wired into `plugin::placeholder`,
proxy installed against a real PotPlayer session):** a naive "poll
`EnumWindows` for a `#32770` titled `Skip Setup`, then `SetWindowPos` it
off-screen once found" loop does not satisfy "no visible flash" — a real,
on-screen flash was observed even though the dialog was found and parked
within about 100ms of the open command being posted. By the time a poll
loop can find the window, it has already been shown once at its default
(owner-centered) position; moving it afterward is measurably too late.

**Fix, also measured live:** a `WH_CBT` hook installed on PotPlayer's own UI
thread (`SetWindowsHookExW(WH_CBT, ..., hMod=nullptr, mainThreadId)` — `hMod`
is `NULL` because the hook procedure lives in the same process as the
target thread; this is an in-process plugin, not cross-process injection)
intercepts window creation before the first paint:

- The drop shadow (`PotShadowWnd`, an app-defined class) is caught at
  `HCBT_CREATEWND`, where its pending `CREATESTRUCT` already carries the
  real class name — rewriting `cs->x`/`cs->y` to an off-screen coordinate
  there means `CreateWindowEx` never actually places it on-screen, not even
  for one frame.
- The dialog itself needs `HCBT_ACTIVATE` instead, not `HCBT_CREATEWND`:
  **measured live** that Skip Setup's `CREATESTRUCT.lpszName` is *not* yet
  `"Skip Setup"` at `HCBT_CREATEWND` time (apparently set later, e.g. via an
  explicit `SetWindowText` during its own `WM_INITDIALOG`) — matching
  against it at creation time silently missed the dialog entirely, leaving
  a real, fully visible, on-screen dialog with nothing left driving it (the
  probe logged a timeout and gave up; the dialog itself stayed open until
  closed by hand). `HCBT_ACTIVATE` fires later, with a real, fully
  initialized `HWND` whose title and class can be read directly
  (`GetWindowTextW`/`GetClassNameW`), still before the system shows/paints
  the window — `SetWindowPos` there reliably relocates it before the first
  paint. This could never work for the shadow: `HCBT_ACTIVATE` never fires
  for a `WS_EX_NOACTIVATE` popup like a drop shadow.

Given both failure modes above cost a real stuck-open dialog and a real
visible flash before landing on this design, the shipped implementation
keeps a poll-based `EnumWindows`/title-match fallback for the case the hook
still misses the dialog for some reason not yet observed — late and
unable to promise "no flash," but guaranteed not to leave a real dialog
sitting open with nothing watching it, which is strictly worse.

**Measured (full live round trip, real PotPlayer, proxy installed):** with
the `HCBT_ACTIVATE` fix in place, `OpenSkipSetup()` opened the dialog in
~55-127ms with no observed flash and no foreground change, resolved all
seven child controls (enable checkbox, range list, Add/Edit/Delete, OK/
Cancel) plus the shadow window, and `CloseSkipSetupCancel()` posted the
Cancel click and confirmed the window actually closed — repeated across two
separate PotPlayer sessions.
