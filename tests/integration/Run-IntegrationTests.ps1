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

If the test video has never had a `.pbf` before, this script attempts a
one-time automated bootstrap add and fails fast with actionable guidance if
that doesn't persist - discovered live: on at least one PotPlayer build,
adding the very FIRST range to a file that's never had a `.pbf` does not
persist when driven by this suite's own out-of-process dialog automation,
even though the identical operation via real hotkeys works fine. See this
folder's README's "Known PotPlayer quirk" section.

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

function Test-Case([string]$Name, [scriptblock]$Body, [int]$Retries = 0) {
    # `Retries` absorbs a real, still-not-fully-root-caused flakiness in
    # this PotPlayer build: Skip Setup's OK occasionally does not persist
    # an Add at all - not a slow write, a write that genuinely never
    # happens, confirmed by waiting 20+ seconds and even closing PotPlayer
    # entirely - even after generous settle delays elsewhere in this
    # suite. Reverting live checks confirmed a failed persist leaves
    # BOTH disk and PotPlayer's own future in-memory reads back at the
    # pre-Add state, never a duplicate or partial one, so redoing the
    # whole scenario body (open, add, close, assert) on failure is safe -
    # nothing accumulates across a retry.
    for ($attempt = 1; $attempt -le ($Retries + 1); $attempt++) {
        try {
            & $Body
            if ($attempt -gt 1) {
                Write-Host "[PASS] $Name (attempt $attempt/$($Retries + 1))" -ForegroundColor Green
            } else {
                Write-Host "[PASS] $Name" -ForegroundColor Green
            }
            $script:passCount++
            return
        } catch {
            if ($attempt -le $Retries) {
                Write-Host "[RETRY] $Name (attempt $attempt/$($Retries + 1) failed: $($_.Exception.Message))" -ForegroundColor Yellow
                continue
            }
            Write-Host "[FAIL] $Name" -ForegroundColor Red
            Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
            $script:failCount++
            return
        }
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
#
# Baseline-aware, not exact-list: checks that `ExpectedCount` entries exist
# in total and that each of `ExpectedPresent` appears SOMEWHERE in the list
# with an exact start/end match, rather than requiring the list to be
# exactly `ExpectedPresent` - see this script's own header comment on the
# discovered "adding to an empty list never persists via automation" quirk.
# Every scenario below runs against a test video that already has a
# baseline range (real precondition, see the bootstrap step), so asserting
# an exact list would make every scenario after the first fail on the
# baseline entry it never asked about.
function Assert-PbfContainsRanges([array]$ExpectedPresent, [int]$ExpectedCount, [string]$PbfPath, [string]$Message, [int]$TimeoutMs = 1000) {
    $actualList = @()
    $ok = $false
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        $actual = Get-PbfRanges -Path $PbfPath
        $actualList = if ($null -eq $actual) { @() } else { @($actual) }

        $ok = ($actualList.Count -eq $ExpectedCount)
        if ($ok) {
            foreach ($want in $ExpectedPresent) {
                $found = $false
                foreach ($have in $actualList) {
                    if ($have.StartMs -eq $want.StartMs -and $have.EndMs -eq $want.EndMs) { $found = $true; break }
                }
                if (-not $found) { $ok = $false; break }
            }
        }
        if (-not $ok) { Start-Sleep -Milliseconds 25 }
    } while (-not $ok -and [DateTime]::UtcNow -lt $deadline)

    if (-not $ok) {
        Write-Host "       --- .pbf mismatch diagnostic ---" -ForegroundColor Yellow
        Write-Host "       Expected count: $ExpectedCount, must contain:" -ForegroundColor Yellow
        foreach ($r in $ExpectedPresent) { Write-Host "         [$($r.StartMs), $($r.EndMs))" -ForegroundColor Yellow }
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

# This suite's own hardcoded ranges (below) top out at 17000ms - confirmed
# live that a range whose end exceeds the clip's real duration makes
# PotPlayer seek past end-of-file when it lands (Skip Interval Setup
# apparently previews the position), which looks exactly like the clip
# finishing playback and can close the file entirely. 20s leaves a safety
# margin rather than cutting it exactly at 17s.
$clipDurationMs = Get-PlaybackDurationMs -MainWindow $mainWindow
if ($clipDurationMs -lt 20000) {
    throw "Test video is only ${clipDurationMs}ms long - this suite's built-in ranges need at least 20000ms " +
        "(a range whose end exceeds the clip's duration can make PotPlayer seek past end-of-file and close it). " +
        "Use a longer throwaway test clip."
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

# ---------------------------------------------------------------------------
# Bootstrap  -  discovered live (not documented anywhere before this suite
# existed): on this PotPlayer build, adding the FIRST range to a video that
# has never had a `.pbf` does not persist when driven by this suite's own
# out-of-process dialog automation (SkipSetupAutomation.psm1) - confirmed
# repeatedly, including waiting 20+ seconds and even closing PotPlayer
# entirely afterward. The SAME operation via the real hotkeys (a genuine
# physical Alt+[/Alt+]/Alt+A, in-process, PTS-014's own commit path) works
# fine and persists immediately - this is specific to the empty-list-to-one
# transition being driven cross-process, not a plugin bug. Every OTHER
# transition (topping up an already non-empty list, clearing one down to
# empty) persists reliably via automation, which is why every scenario
# below is written to add ON TOP of a baseline rather than assuming it
# starts from a clean empty list.
if ($null -eq $originalBytes) {
    Write-Host "No baseline range on this file yet - attempting a one-time automated bootstrap add..."
    Invoke-WithSkipSetupDialog {
        param($dialog)
        [void](Set-SkipSetupEnabled -Dialog $dialog)
        [void](Add-SkipSetupRange -Dialog $dialog -StartMs 500 -EndMs 1500)
        Close-SkipSetupDialog -Dialog $dialog -Ok
    }
    $bootstrapped = $false
    $deadline = [DateTime]::UtcNow.AddMilliseconds(2000)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $pbfPath -PathType Leaf) { $bootstrapped = $true; break }
        Start-Sleep -Milliseconds 50
    }
    if (-not $bootstrapped) {
        throw "Bootstrap failed: this PotPlayer build did not persist the first automated Add to a file with no prior " +
            "`.pbf` (a known quirk of driving Skip Setup out-of-process - see this folder's README). Add one skip " +
            "range to '$TestVideoPath' by hand once - press Alt+[, then Alt+], then Alt+A while it's playing, or use " +
            "Skip Setup's own Add... yourself - then re-run this suite. Real hotkeys do not have this limitation."
    }
    Write-Host "Bootstrap succeeded - baseline range established."
}

try {
    # Every scenario below adds ON TOP of whatever's already on the file
    # (the pre-existing content backed up above, or this run's own
    # bootstrap range) rather than clearing first - see this script's own
    # "Bootstrap" section for why starting an Add from a verified-empty
    # list can't be driven reliably by this suite's own automation. Clear
    # is exercised exactly once, last, with nothing added afterward.
    $baselineCount = 0
    Invoke-WithSkipSetupDialog {
        param($dialog)
        $script:baselineCount = Get-SkipSetupRangeCount -Dialog $dialog
    }
    Write-Host "Baseline range count on this file: $baselineCount"

    # -----------------------------------------------------------------
    # Scenario: add one range via the dialog-driving primitives with a
    # known, exact timestamp, and assert it lands in the .pbf
    # millisecond-exact (PTS-018's first acceptance criterion).
    # -----------------------------------------------------------------
    $firstRange = [PSCustomObject]@{ StartMs = 3000; EndMs = 5567 }
    Test-Case -Retries 2 -Name "Add via Skip Setup with a known timestamp -> .pbf matches millisecond-exact" -Body {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            [void](Set-SkipSetupEnabled -Dialog $dialog)
            [void](Add-SkipSetupRange -Dialog $dialog -StartMs $firstRange.StartMs -EndMs $firstRange.EndMs)
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        Assert-PbfContainsRanges -ExpectedPresent @($firstRange) -ExpectedCount ($baselineCount + 1) -PbfPath $pbfPath -Message ".pbf after first Add"
    }

    # -----------------------------------------------------------------
    # Scenario: additive - a second Add tops up rather than replacing.
    # -----------------------------------------------------------------
    $secondRange = [PSCustomObject]@{ StartMs = 8000; EndMs = 10250 }
    Test-Case -Retries 2 -Name "A second Add tops up (additive), both ranges present and exact" -Body {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            Assert-Equal ($baselineCount + 1) (Wait-SkipSetupRangeCount -Dialog $dialog -Expected ($baselineCount + 1)) "range count before second Add"
            [void](Add-SkipSetupRange -Dialog $dialog -StartMs $secondRange.StartMs -EndMs $secondRange.EndMs)
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        Assert-PbfContainsRanges -ExpectedPresent @($firstRange, $secondRange) -ExpectedCount ($baselineCount + 2) -PbfPath $pbfPath -Message ".pbf after additive second Add"
    }

    # -----------------------------------------------------------------
    # Scenario: the enable-skip guard - EnsureSkipEnabled turns the
    # checkbox on when it was off, and the resulting Add still lands
    # correctly (PTS-012's "a range added while it's off would silently
    # never skip anything" concern).
    # -----------------------------------------------------------------
    $guardRange = [PSCustomObject]@{ StartMs = 15000; EndMs = 17000 }
    Test-Case -Retries 2 -Name "Enable-skip guard: turning the checkbox off, then Set-SkipSetupEnabled turns it back on" -Body {
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
        Assert-PbfContainsRanges -ExpectedPresent @($guardRange) -ExpectedCount ($baselineCount + 3) -PbfPath $pbfPath -Message ".pbf after enable-guard Add"
    }

    # -----------------------------------------------------------------
    # Scenario: clear-all deletes the whole .pbf file, not just empties it
    # (docs/FINDINGS.md section 1: "when the last entry is deleted... the
    # whole .pbf file is deleted, not left empty"). Runs LAST among the
    # automation-driven scenarios - nothing after this adds anything via
    # SkipSetupAutomation.psm1, since a subsequent Add from this
    # now-verified-empty list would hit the same quirk the bootstrap step
    # exists to route around.
    # -----------------------------------------------------------------
    Test-Case -Retries 2 -Name "Clear removes all ranges -> .pbf file is deleted, not emptied" -Body {
        Invoke-WithSkipSetupDialog {
            param($dialog)
            Assert-Equal ($baselineCount + 3) (Wait-SkipSetupRangeCount -Dialog $dialog -Expected ($baselineCount + 3)) "range count before clear"
            Clear-SkipSetupRanges -Dialog $dialog
            Assert-Equal 0 (Get-SkipSetupRangeCount -Dialog $dialog) "range count after clear (in-dialog)"
            Close-SkipSetupDialog -Dialog $dialog -Ok
        }
        # Same Close-SkipSetupDialog-doesn't-wait-for-the-file-write race
        # Assert-PbfContainsRanges polls for above, just for "file is gone"
        # instead of "file matches."
        $deadline = [DateTime]::UtcNow.AddMilliseconds(1000)
        while ((Test-Path -LiteralPath $pbfPath -PathType Leaf) -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 25
        }
        Assert-True (-not (Test-Path -LiteralPath $pbfPath -PathType Leaf)) ".pbf should no longer exist after clearing the last range"
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
                Assert-PbfContainsRanges -ExpectedPresent @([PSCustomObject]@{ StartMs = $posAfterStart; EndMs = $posAfterEnd }) -ExpectedCount $countAfter -PbfPath $pbfPath `
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
