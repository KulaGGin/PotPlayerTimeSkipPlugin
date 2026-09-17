# REVERIFICATION_CHECKLIST.md — bringing the plugin up to a new PotPlayer version

PTS-017's counterpart to docs/FINDINGS.md: everything in that file is tagged
to the exact `PotPlayerMini64.exe` build it was measured against, and a
version bump invalidates every control-id, window-class, and export-count
claim in it until it's re-checked. This checklist is what makes that
re-check a bounded, mechanical task instead of a fresh reverse-engineering
session.

## 0. Let the plugin tell you first

Before doing anything manually, run the plugin's own self-check (PTS-017):
open PotPlayer with the plugin installed, press any of the three hotkeys
once, then check the result:

```powershell
powershell -File tools\install\potplayer-proxy.ps1 Status
```

- `SelfCheckResult = Pass` — the dialog geography below still matches;
  nothing to re-measure.
- `SelfCheckResult = Fail` — `SelfCheckDetail` names exactly which stage
  broke (`main window`, `Skip Setup dialog`, or `Skip Interval Setup
  dialog`) and, where relevant, which control id went missing. Start your
  manual re-measurement there, not from scratch.
- `SelfCheckResult = NeverRun` — no hotkey has been pressed yet against this
  install; press one first.

The self-check only ever *detects* drift — it never re-derives the new
control ids for you. The rest of this checklist is how to do that by hand,
the same way docs/FINDINGS.md's own measurements were originally taken.

## 1. Update the provenance line

In docs/FINDINGS.md's "Provenance" section, record the new
`PotPlayerMini64.exe` build date/version before changing anything else, so
every measurement below is dated against it.

## 2. Re-check the main window class (docs/FINDINGS.md section 4)

- Launch the new PotPlayer build, open a file.
- Enumerate its top-level windows and confirm the main window's class is
  still `PotPlayer64`. If it changed, update `kMainWindowClass` in
  `plugin/src/player_window.cpp`.

## 3. Re-check the WM_USER playback queries (docs/FINDINGS.md section 4)

- Confirm `SendMessage(hwnd, 0x0400, 0x5004/0x5002/0x5006, 0)` still returns
  position/duration/status as expected. Cross-check position against a
  dialog that shows the current playback time (e.g. Skip Interval Setup,
  see below), same method the original measurement used.

## 4. Re-check Skip Setup's command id and controls (docs/FINDINGS.md section 3, 7)

- Confirm `kOpenSkipSetupCommandId` (`10240`) still opens Skip Setup. If the
  command id moved, re-derive it by loading the new `PotPlayer64.dll` with
  `LOAD_LIBRARY_AS_DATAFILE` and walking its `RT_MENU`/`RT_ACCELERATOR`
  resources for the Playback Skip entry, exactly as the original PTS-010
  measurement did — no live PotPlayer run needed for this step.
- With Skip Setup open, re-verify every control id in
  `plugin/include/plugin/skip_setup.hpp`'s table (enable checkbox, range
  list, Add/Edit/Delete, OK/Cancel) still resolves via `GetDlgItem`. Update
  the `kEnableCheckboxId`/`kRangeListId`/etc. constants for whichever moved.

## 5. Re-check Skip Interval Setup's controls (docs/FINDINGS.md section 3)

- Click Add... and re-verify the Start/End/Length edits, Type combo, and
  OK/Cancel ids in the same file's second table. Update
  `kIntervalStartEditId`/etc. for whichever moved.
- Re-confirm `kFileSpecificComboIndex` (`1`) is still File-specific, not
  Overall — the combo's item order is exactly the kind of thing a UI
  refresh could silently reorder.

## 6. Re-check the `.pbf` format (docs/FINDINGS.md section 1)

- Add one range through the (possibly-updated) dialogs and diff the
  resulting `.pbf` against the documented
  `<index>=<type>*<start_ms>*<length_ms>` format. This is app-version-
  independent in principle (it's PotPlayer's on-disk format, not a dialog
  detail) but cheap to re-confirm alongside everything else.

## 7. Re-check MediaDB64.dll's exports (docs/FINDINGS.md section 5, PTS-005/006)

- Already automated: `powershell -File tools\install\potplayer-proxy.ps1
  Install` (and `Repair`) refuse outright if the new `MediaDB64.dll`'s
  exports don't match the proxy's forwarder set (see
  `Test-ForwarderSetMatches` in `tools/install/PotPlayerProxy.psm1`) — if
  that refusal fires, `dumpbin /exports` (or `Get-PEExportInfo`) the new DLL
  and compare against `proxy/`'s `/EXPORT` pragmas.

## 8. Update every changed constant, rebuild, re-run the self-check

After updating whichever constants moved, rebuild
(`cmake --build build --config Debug`), reinstall the proxy, and re-run step
0's self-check to confirm it now passes clean. Update docs/FINDINGS.md with
the new measurements (measured, not inferred) alongside the old ones,
following that file's own "call out what changed" discipline.
