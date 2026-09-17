<#
.SYNOPSIS
PTS-018's opt-in end-to-end integration suite: exercises the whole plugin
against a LIVE PotPlayer and verifies the ground truth, the `.pbf` on disk.

.DESCRIPTION
Unlike tests/unit (CTest, fast, no external state) and this folder's own
PbfCodec.Tests.ps1 (fast, no PotPlayer needed), this script needs:

  1. The proxy installed against a real PotPlayer (tools/install/potplayer-proxy.ps1
     Install), and PotPlayer running.
  2. A throwaway test video file already OPEN in that PotPlayer session -
     never point this at a file from your real library: every test case
     here adds/clears real skip ranges against whatever file is open, and
     while the original `.pbf` state is always backed up and restored (see
     the try/finally below), a PotPlayer crash mid-run would skip that
     restore.

It is never wired into CTest and never runs as part of `ctest --test-dir
build`: PTS-018's own acceptance criteria call for it to stay "separate from
the fast unit run," reachable only by explicitly running this script.

.PARAMETER TestVideoPath
Full path to the test video currently open in PotPlayer. Required - this
script refuses to guess which file is open from inside PotPlayer's own
state, since a wrong guess here would corrupt the wrong file's `.pbf`.

.PARAMETER SkipHotkeyGestureTest
Skips the real-Alt-key-chord test case (HotkeyGesture.psm1). Use this if
you can't get SetForegroundWindow to stick in your session (see this
folder's README's "Foreground and the hotkey gesture test" section) - every
other test case drives the dialogs directly and doesn't need PotPlayer to be
foreground at all.

.EXAMPLE
powershell -File tests\integration\Run-IntegrationTests.ps1 -TestVideoPath 'C:\path\to\throwaway-clip.mp4'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$TestVideoPath,
    [switch]$SkipHotkeyGestureTest
)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'PbfCodec.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Win32Interop.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SkipSetupAutomation.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'PlayerQuery.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'HotkeyGesture.psm1') -Force

# ---------------------------------------------------------------------------
# Tiny test harness  -  same shape as tools/install/Tests.ps1 and this
# folder's own PbfCodec.Tests.ps1, reused rather than reinvented.
# ---------------------------------------------------------------------------
$script:passCount = 0
$script:failCount = 0

function Test-Case([string]$Name, [scriptblock]$Body) {
    try {
        & $Body
        Write-Host "[PASS] $Name" -ForegroundColor Green
        $script:passCount++
    } catch {
        Write-Host "[FAIL] $Name" -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        $script:failCount++
    }
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "Assertion failed: $Message" }
}

function Assert-Equal($Expected, $Actual, [string]$Message) {
    if ($Expected -ne $Actual) {
        throw "Assertion failed: $Message (expected '$Expected', got '$Actual')"
    }
}

# Opens Skip Setup, runs `Body` against it, and guarantees the dialog is
# closed (Cancel) afterward even if `Body` throws  -  a Test-Case failure
# partway through a scenario must never leave a dialog dangling open for the
# next scenario's Open-SkipSetupDialog to trip over.
function Invoke-WithSkipSetupDialog([scriptblock]$Body) {
    $dialog = Open-SkipSetupDialog
    try {
        & $Body $dialog
    } finally {
        if ([PotPlayerTimeSkip.Win32]::IsWindow($dialog.Hwnd)) {
            try { Close-SkipSetupDialog -Dialog $dialog -Cancel } catch { }
        }
    }
}

# Dumps requested vs. actual .pbf content on a mismatch (PTS-018's own
# "make failures diagnostic" design note) rather than leaving the caller to
# go decode the file by hand. Polls briefly first: Close-SkipSetupDialog
# only waits for the WINDOW to disappear (Wait-WindowGone), not for
# PotPlayer's own file write to land, so reading the instant the window
# handle goes invalid is a real (if narrow) race, not a hypothetical one.
function Assert-PbfRangesMatch([array]$Expected, [string]$PbfPath, [string]$Message, [int]$TimeoutMs = 1000) {
    # Named $rangesEqual, not $matches: $matches is PowerShell's automatic
    # variable populated by the -match operator - shadowing it here would be
    # a landmine for the next edit that adds a -match check to this function.
    $actualList = @()
    $rangesEqual = $false
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $actual = Get-PbfRanges -Path $PbfPath
        $actualList = if ($null -eq $actual) { @() } else { @($actual) }

        $rangesEqual = ($actualList.Count -eq $Expected.Count)
        if ($rangesEqual) {
            for ($i = 0; $i -lt $Expected.Count; $i++) {
                if ($actualList[$i].StartMs -ne $Expected[$i].StartMs -or $actualList[$i].EndMs -ne $Expected[$i].EndMs) {
                    $rangesEqual = $false
                    break
                }
            }
        }
        if (-not $rangesEqual) { Start-Sleep -Milliseconds 25 }
    } while (-not $rangesEqual -and [DateTime]::UtcNow -lt $deadline)

    if (-not $rangesEqual) {
        Write-Host "       --- .pbf mismatch diagnostic ---" -ForegroundColor Yellow
        Write-Host "       Requested:" -ForegroundColor Yellow
        foreach ($r in $Expected) { Write-Host "         [$($r.StartMs), $($r.EndMs))" -ForegroundColor Yellow }
        Write-Host "       Actual ($PbfPath):" -ForegroundColor Yellow
        foreach ($r in $actualList) { Write-Host "         index=$($r.Index) type=$($r.Type) [$($r.StartMs), $($r.EndMs))" -ForegroundColor Yellow }
        throw "Assertion failed: $Message"
    }
}

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $TestVideoPath -PathType Leaf)) {
    throw "TestVideoPath '$TestVideoPath' does not exist."
}

Write-Host "Locating PotPlayer's main window..."
$mainWindow = Find-PotPlayerMainWindow
Write-Host "Found main window handle $mainWindow."

if (-not (Test-PotPlayerFileOpen -MainWindow $mainWindow)) {
    throw "PotPlayer reports no file open (duration query returned 0). Open '$TestVideoPath' in PotPlayer first."
}

$pbfPath = Get-PbfPath -VideoPath $TestVideoPath
Write-Host "Test video: $TestVideoPath"
Write-Host ".pbf sidecar: $pbfPath"

# ---------------------------------------------------------------------------
# Backup/restore  -  PTS-018's "a cleanup step that always restores the test
# file's .pbf to its prior state" acceptance criterion. Captured as raw
# bytes (not re-derived from parsed ranges) so the restore is byte-exact
# regardless of what this suite's own codec does or doesn't understand about
# the file, and $null (file absent) is itself a valid backed-up state.
# ---------------------------------------------------------------------------
$originalBytes = $null
if (Test-Path -LiteralPath $pbfPath -PathType Leaf) {
    $originalBytes = [System.IO.File]::ReadAllBytes($pbfPath)
    Write-Host "Backed up existing .pbf ($($originalBytes.Length) bytes)."
} else {
    Write-Host "No existing .pbf - will restore to 'absent' afterward."
}

try {
    # -----------------------------------------------------------------
    # Scenario: add one range via the dialog-driving primitives with a
    # known, exact timestamp, and assert the .pbf matches millisecond-exact
    # (PTS-018's first acceptance criterion).
    # -----------------------------------------------------------------
    $firstRange = [PSCustomObject]@{ StartMs = 12000; EndMs = 34567 }
    Test-Case "Add via Skip Setup with a known timestamp -> .pbf matches millisecond-exact" {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            Clear-SkipSetupRanges -Dialog $dialog
            [void](Set-SkipSetupEnabled -Dialog $dialog)
            [void](Add-SkipSetupRange -Dialog $dialog -StartMs $firstRange.StartMs -EndMs $firstRange.EndMs)
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        Assert-PbfRangesMatch -Expected @($firstRange) -PbfPath $pbfPath -Message ".pbf after first Add"
    }

    # -----------------------------------------------------------------
    # Scenario: additive - a second Add tops up rather than replacing.
    # -----------------------------------------------------------------
    $secondRange = [PSCustomObject]@{ StartMs = 100000; EndMs = 105250 }
    Test-Case "A second Add tops up (additive), both ranges present and exact" {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            Assert-Equal 1 (Get-SkipSetupRangeCount -Dialog $dialog) "range count before second Add"
            [void](Add-SkipSetupRange -Dialog $dialog -StartMs $secondRange.StartMs -EndMs $secondRange.EndMs)
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        Assert-PbfRangesMatch -Expected @($firstRange, $secondRange) -PbfPath $pbfPath -Message ".pbf after additive second Add"
    }

    # -----------------------------------------------------------------
    # Scenario: clear-all deletes the whole .pbf file, not just empties it
    # (docs/FINDINGS.md section 1: "when the last entry is deleted... the
    # whole .pbf file is deleted, not left empty").
    # -----------------------------------------------------------------
    Test-Case "Clear removes all ranges -> .pbf file is deleted, not emptied" {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            Assert-Equal 2 (Get-SkipSetupRangeCount -Dialog $dialog) "range count before clear"
            Clear-SkipSetupRanges -Dialog $dialog
            Assert-Equal 0 (Get-SkipSetupRangeCount -Dialog $dialog) "range count after clear (in-dialog)"
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        # Same Close-SkipSetupDialog-doesn't-wait-for-the-file-write race
        # Assert-PbfRangesMatch polls for above, just for "file is gone"
        # instead of "file matches."
        $deadline = [DateTime]::UtcNow.AddMilliseconds(1000)
        while ((Test-Path -LiteralPath $pbfPath -PathType Leaf) -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 25
        }
        Assert-True (-not (Test-Path -LiteralPath $pbfPath -PathType Leaf)) ".pbf should no longer exist after clearing the last range"
    }

    # -----------------------------------------------------------------
    # Scenario: the enable-skip guard - EnsureSkipEnabled turns the
    # checkbox on when it was off, and the resulting Add still lands
    # correctly (PTS-012's "a range added while it's off would silently
    # never skip anything" concern).
    # -----------------------------------------------------------------
    $guardRange = [PSCustomObject]@{ StartMs = 5000; EndMs = 9000 }
    Test-Case "Enable-skip guard: turning the checkbox off, then Set-SkipSetupEnabled turns it back on" {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            if (Get-SkipSetupEnabled -Dialog $dialog) {
                Invoke-CheckboxClick -ControlHandle $dialog.EnableCheckbox
            }
            Assert-True (-not (Get-SkipSetupEnabled -Dialog $dialog)) "checkbox should read back unchecked after turning it off"

            $result = Set-SkipSetupEnabled -Dialog $dialog
            Assert-True $result "Set-SkipSetupEnabled should report success"
            Assert-True (Get-SkipSetupEnabled -Dialog $dialog) "checkbox should read back checked after Set-SkipSetupEnabled"

            [void](Add-SkipSetupRange -Dialog $dialog -StartMs $guardRange.StartMs -EndMs $guardRange.EndMs)
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        Assert-PbfRangesMatch -Expected @($guardRange) -PbfPath $pbfPath -Message ".pbf after enable-guard Add"
    }

    # Reset to empty before the hotkey-gesture test, so its own assertions
    # aren't entangled with the guard-test range above.
    Invoke-WithSkipSetupDialog {
        param($dialog)
        Clear-SkipSetupRanges -Dialog $dialog
        Close-SkipSetupDialog -Dialog $dialog -Ok
    }

    # -----------------------------------------------------------------
    # Scenario: the real three-key gesture (Alt+[ / Alt+] / Alt+A), not the
    # dialog-driving primitives - see HotkeyGesture.psm1's own header for
    # why this can only assert shape (start < end, something got added),
    # never an exact millisecond value. Also checks PTS-010's "no focus
    # stolen, nothing flashed" claim in the one weak sense observable from
    # outside the process: the foreground window is back to PotPlayer
    # (or unchanged) once the commit settles.
    # -----------------------------------------------------------------
    if ($SkipHotkeyGestureTest) {
        Write-Host "[SKIP] real three-key gesture (Alt+[ / Alt+] / Alt+A) test (-SkipHotkeyGestureTest)" -ForegroundColor Yellow
    } else {
        Test-Case "The real three-key gesture (Alt+[ / Alt+] / Alt+A) adds a range end-to-end" {
            if (-not (Set-ForegroundWindowSafe -Handle $mainWindow)) {
                throw "could not bring PotPlayer to the foreground - see README's 'Foreground and the hotkey gesture test' section, or re-run with -SkipHotkeyGestureTest"
            }

            $countBefore = 0
            Invoke-WithSkipSetupDialog {
                param($dialog)
                $script:countBefore = Get-SkipSetupRangeCount -Dialog $dialog
            }

            Send-MarkStartHotkey
            $posAfterStart = Get-PlaybackPositionMs -MainWindow $mainWindow
            Send-MarkEndHotkey
            $posAfterEnd = Get-PlaybackPositionMs -MainWindow $mainWindow

            Send-CommitMarkHotkey

            $foregroundAfter = Get-ForegroundWindowHandle
            Assert-True ($foregroundAfter -eq $mainWindow) "foreground window should still be PotPlayer's main window after the gesture (no focus stolen)"

            $countAfter = 0
            Invoke-WithSkipSetupDialog {
                param($dialog)
                $script:countAfter = Get-SkipSetupRangeCount -Dialog $dialog
            }

            if ($countAfter -eq $countBefore -and $posAfterEnd -le $posAfterStart) {
                Write-Host "       (playback position did not advance between Alt+[ and Alt+] - paused test clip? this is expected to no-op, not fail)" -ForegroundColor Yellow
            } else {
                Assert-Equal ($countBefore + 1) $countAfter "range count should go up by exactly one after Alt+[ Alt+] Alt+A"
                Assert-PbfRangesMatch -Expected @([PSCustomObject]@{ StartMs = $posAfterStart; EndMs = $posAfterEnd }) -PbfPath $pbfPath `
                    -Message ".pbf after the hotkey-gesture commit"
            }

            Invoke-WithSkipSetupDialog {
                param($dialog)
                Clear-SkipSetupRanges -Dialog $dialog
                Close-SkipSetupDialog -Dialog $dialog -Ok
            }
        }
    }
} finally {
    Write-Host ""
    Write-Host "Restoring original .pbf state..."
    if ($null -eq $originalBytes) {
        if (Test-Path -LiteralPath $pbfPath -PathType Leaf) {
            Remove-Item -LiteralPath $pbfPath -Force
            Write-Host "Removed .pbf (none existed before this run)."
        } else {
            Write-Host "Nothing to restore (no .pbf before or after)."
        }
    } else {
        [System.IO.File]::WriteAllBytes($pbfPath, $originalBytes)
        Write-Host "Restored original .pbf ($($originalBytes.Length) bytes)."
    }
}

Write-Host ""
Write-Host "$script:passCount passed, $script:failCount failed" -ForegroundColor ($(if ($script:failCount -eq 0) { 'Green' } else { 'Red' }))
if ($script:failCount -gt 0) { exit 1 }
exit 0
