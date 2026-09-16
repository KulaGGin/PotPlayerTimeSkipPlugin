# tools/install — PTS-007

Safe, reversible, idempotent scripts to put the `MediaDB64.dll` proxy in
place, take it out, and repair it after a PotPlayer update. Pure PowerShell,
no external tools (no `dumpbin`, no modules to install) — PE export tables
are parsed directly from the file bytes.

## Layout

- [`PotPlayerProxy.psm1`](PotPlayerProxy.psm1) — the module with all the
  logic: PE export parsing, install-state detection, and the
  install/uninstall/repair/status operations.
- [`potplayer-proxy.ps1`](potplayer-proxy.ps1) — the CLI. Handles elevation
  (the install dir is under `Program Files`) and detecting a running
  PotPlayer (a loaded DLL can't be replaced), then delegates to the module.
- [`Tests.ps1`](Tests.ps1) — scripted dry-run tests against a synthetic copy
  of the install layout. No real PotPlayer install needed.

## Usage

From the repo root, or anywhere:

```powershell
# Report whether a proxy is installed, the PotPlayer version, and whether
# the on-disk original still matches what the proxy expects.
powershell -File tools\install\potplayer-proxy.ps1 Status

# Install: back up the real MediaDB64.dll, drop the proxy in.
powershell -File tools\install\potplayer-proxy.ps1 Install

# Uninstall: restore the real MediaDB64.dll, remove the proxy.
powershell -File tools\install\potplayer-proxy.ps1 Uninstall

# Repair: re-wrap PotPlayer's MediaDB64.dll after an update overwrote it.
powershell -File tools\install\potplayer-proxy.ps1 Repair
```

All actions default to `C:\Program Files\PotPlayer` (pass `-InstallDir` for
a different install) and, for `Install`/`Repair`, to the most recently built
`build\proxy\*\MediaDB64.dll` (pass `-ProxyPath` to override).

If the install directory isn't writable, the script warns and relaunches
itself elevated (one UAC prompt). If PotPlayer is running, it warns and
waits for you to close it before touching any files.

## Safety model

- **Never destroys the real DLL.** Every mutating step is a rename or a
  copy; the original is only ever renamed aside (`MediaDB64_orig.dll`), and
  a partial failure mid-operation rolls back rather than leaving PotPlayer
  without a `MediaDB64.dll` at all.
- **Refuses to guess.** `Get-ProxyInstallState` classifies the directory
  into `NotInstalled` / `Installed` / `UpdateDetected` / `Missing` /
  `Unknown`. Anything that doesn't fit one of the first three is `Unknown`,
  and every operation refuses to touch an `Unknown` or otherwise
  unexpected state rather than guessing.
- **Verifies exports before swapping.** Before installing or repairing, the
  proxy's forwarder set (read from its own export table, not hardcoded) is
  checked against the target DLL's real exports. A mismatch — a PotPlayer
  version whose `MediaDB64.dll` doesn't export what the proxy forwards to —
  is refused with the specific missing export named, not silently patched
  over.
- **Idempotent.** Re-running `Install` on an existing install, `Uninstall`
  on a pristine one, or `Repair` on an already-repaired one is a no-op that
  reports the current state rather than erroring or double-acting.

## Testing

```powershell
powershell -File tools\install\Tests.ps1
```

Runs entirely against a temp directory and synthetic PE files (the real
`MediaDB64.dll` is proprietary and never committed — see `.gitignore`), so
it needs no PotPlayer install and no build. It covers: fresh install,
double-install refusal, uninstall restore (byte-for-byte), post-update
repair, and exports-mismatch refusal on both `Install` and `Repair`.

## Out of scope

- Auto-updating the proxy when PotPlayer updates itself — running `Repair`
  manually after an update is enough.
- A GUI installer.
