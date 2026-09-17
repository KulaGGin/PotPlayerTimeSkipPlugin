# SkipSetupAutomation.psm1  -  PTS-018's cross-process twin of
# plugin/src/skip_setup.cpp.
#
# Drives the real Skip Setup / Skip Interval Setup dialogs from OUTSIDE
# PotPlayer's process, using the exact control ids and message sequences
# docs/FINDINGS.md section 3 measured and plugin/src/skip_setup.cpp already
# implements in-process (PTS-010/011/012). This is the "exported primitives"
# half of the issue's "drives the real three-key gesture (or calls the
# exported primitives)" — the plugin has no literal exported DLL call for
# this, so this module re-derives the same dialog automation cross-process,
# which is exactly how docs/FINDINGS.md section 3's own prototype validated
# this dialog geography before PTS-010 moved it in-process.
#
# Unlike the shipped plugin, this module makes NO attempt to hide the
# dialogs it drives (no WH_CBT off-screen parking — that's a same-process,
# same-thread mechanism PTS-010 uses, not available across a process
# boundary): the dialogs WILL flash on screen while this runs. That's fine
# for a test harness whose whole point is to observe ground truth, as
# opposed to the shipped feature's point of never being seen.
#
# Deliberately does NOT go through core::FormatTimecode's C++ implementation
# (there's no way to call into it from PowerShell without building a
# separate bridge) — Format-SkipTimecode below is a small, independently
# written port of the same "HH:MM:SS.mmm" shape, so a bug in one isn't
# masked by an identical bug in the other.

Set-StrictMode -Version Latest

# No -Force here: this module is itself re-imported with -Force by callers
# (e.g. Run-IntegrationTests.ps1), and Import-Module -Force on an
# already-loaded dependency from INSIDE another module strips that
# dependency's functions back out of the caller's global session (a real
# PowerShell gotcha, confirmed live: Win32Interop's own exports vanished
# from the session the moment this module's import of it used -Force,
# even though the top-level script had already imported Win32Interop
# directly moments before). Plain Import-Module is idempotent when the
# module's already loaded by this same path, so this only actually loads
# it the first time - which is all a dependency needs.
Import-Module (Join-Path $PSScriptRoot 'Win32Interop.psm1')

# Control ids, straight out of plugin/include/plugin/skip_setup.hpp.
$script:kMainWindowClass = 'PotPlayer64'
$script:kOpenSkipSetupCommandId = 10240
$script:kEnableCheckboxId = 3034
$script:kRangeListId = 3242
$script:kAddButtonId = 3024
$script:kDeleteButtonId = 3026
$script:kOkButtonId = 1
$script:kCancelButtonId = 2
$script:kSkipSetupTitle = 'Skip Setup'

$script:kIntervalStartEditId = 3088
$script:kIntervalEndEditId = 3092
$script:kIntervalTypeComboId = 3012
$script:kIntervalOkButtonId = 1
$script:kIntervalCancelButtonId = 2
$script:kFileSpecificComboIndex = 1
$script:kSkipIntervalTitle = 'Skip Interval Setup'

function Format-SkipTimecode {
    <#
    .SYNOPSIS
    ms -> "HH:MM:SS.mmm", the exact shape core::FormatTimecode produces
    (core/src/core.cpp) and Skip Interval Setup's Start/End edits expect.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][int64]$Ms)

    if ($Ms -lt 0) { throw "Format-SkipTimecode: cannot format a negative duration ($Ms)" }
    # PowerShell's `/` always produces a double and [int]/[int64] casts
    # ROUND rather than truncate (754/60 = 12.566... -> [int] gives 13, not
    # 12) - caught live by test_skipsetup_import.ps1 while writing this
    # module. [math]::Floor + explicit remainder-by-subtraction avoids ever
    # feeding a non-integer value through a rounding cast.
    $totalSeconds = [int64][math]::Floor($Ms / 1000.0)
    $millis = [int]($Ms - ($totalSeconds * 1000))
    $hours = [int64][math]::Floor($totalSeconds / 3600.0)
    $remainderAfterHours = $totalSeconds - ($hours * 3600)
    $minutes = [int][math]::Floor($remainderAfterHours / 60.0)
    $seconds = [int]($remainderAfterHours - ($minutes * 60))
    return "{0:D2}:{1:D2}:{2:D2}.{3:D3}" -f $hours, $minutes, $seconds, $millis
}

function ConvertFrom-SkipTimecode {
    <#
    .SYNOPSIS
    The inverse of Format-SkipTimecode, for verifying a dialog's read-back
    text  -  mirrors core::ParseTimecode's exact-shape requirement (no
    tolerance for reformatting).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Text)

    if ($Text -notmatch '^(\d+):(\d{2}):(\d{2})\.(\d{3})$') {
        throw "ConvertFrom-SkipTimecode: '$Text' is not HH:MM:SS.mmm shaped"
    }
    $hours = [int64]$Matches[1]
    $minutes = [int64]$Matches[2]
    $seconds = [int64]$Matches[3]
    $millis = [int64]$Matches[4]
    if ($minutes -gt 59 -or $seconds -gt 59) {
        throw "ConvertFrom-SkipTimecode: '$Text' has an out-of-range minutes/seconds field"
    }
    return ((($hours * 60) + $minutes) * 60 + $seconds) * 1000 + $millis
}

function Find-PotPlayerMainWindow {
    [CmdletBinding()]
    param()
    $h = Find-WindowByClass -ClassName $script:kMainWindowClass
    if (-not $h) { throw "Find-PotPlayerMainWindow: no window of class '$script:kMainWindowClass' found - is PotPlayer running with a file open?" }
    return $h
}

function Open-SkipSetupDialog {
    <#
    .OUTPUTS
    PSCustomObject { Hwnd, EnableCheckbox, RangeList, AddButton, DeleteButton, OkButton, CancelButton }
    #>
    [CmdletBinding()]
    param([int]$TimeoutMs = 3000)

    $mainWindow = Find-PotPlayerMainWindow
    [void](Send-WmCommandClick -WindowHandle $mainWindow -Id $script:kOpenSkipSetupCommandId -ControlHandle ([IntPtr]::Zero))
    # This is the one open command this suite posts directly to the main
    # window rather than through Send-WmCommandClick's BN_CLICKED shape
    # (there's no button here, it's a menu/accelerator command id) - wParam
    # is just the plain command id with notify code 0, matching
    # skip_setup.cpp's MAKEWPARAM(kOpenSkipSetupCommandId, 0).

    $dlg = Find-DialogByTitle -Title $script:kSkipSetupTitle -TimeoutMs $TimeoutMs
    if (-not $dlg) { throw "Open-SkipSetupDialog: Skip Setup did not appear within ${TimeoutMs}ms" }

    $controls = [PSCustomObject]@{
        Hwnd            = $dlg
        EnableCheckbox  = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kEnableCheckboxId
        RangeList       = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kRangeListId
        AddButton       = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kAddButtonId
        DeleteButton    = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kDeleteButtonId
        OkButton        = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kOkButtonId
        CancelButton    = Get-DlgItemHandle -DialogHandle $dlg -Id $script:kCancelButtonId
    }
    foreach ($prop in 'EnableCheckbox', 'RangeList', 'AddButton', 'DeleteButton', 'OkButton', 'CancelButton') {
        if (-not $controls.$prop) {
            throw "Open-SkipSetupDialog: Skip Setup opened but control '$prop' was not found - layout mismatch (see docs/REVERIFICATION_CHECKLIST.md)"
        }
    }

    # Confirmed live (repeatedly) that a freshly-found dialog's range list
    # can read back LVM_GETITEMCOUNT == 0 - genuinely STABLY, for a whole
    # settle window, not just a flicker - before PotPlayer's own
    # WM_INITDIALOG finishes populating the ListView from its true
    # in-memory list. A same-process "did two reads agree" check can't
    # tell "stably wrong" apart from "stably right" without knowing what
    # the right answer should be, so that responsibility belongs to the
    # caller: use Wait-SkipSetupRangeCount below when a specific count is
    # expected here, rather than trusting Get-SkipSetupRangeCount's first
    # read right after an open.
    Start-Sleep -Milliseconds 150

    return $controls
}

function Close-SkipSetupDialog {
    [CmdletBinding(DefaultParameterSetName = 'Ok')]
    param(
        [Parameter(Mandatory)][PSCustomObject]$Dialog,
        [Parameter(ParameterSetName = 'Ok')][switch]$Ok,
        [Parameter(ParameterSetName = 'Cancel')][switch]$Cancel,
        [int]$TimeoutMs = 3000
    )
    if ($Cancel) {
        [void](Send-WmCommandClick -WindowHandle $Dialog.Hwnd -Id $script:kCancelButtonId -ControlHandle $Dialog.CancelButton)
    } else {
        # Settle delay before OK only (never needed for Cancel, which
        # persists nothing). Confirmed live: this automation, run with no
        # delay at all between the preceding Add/checkbox-toggle and this
        # click, intermittently failed to persist even a routine top-up
        # add (not just the empty-list case) - a real human's own
        # click-then-click pacing never hits this, only a script firing
        # PostMessage calls back-to-back does. Not root-caused further
        # (would need a debugger attached to PotPlayer itself); this is a
        # pragmatic mitigation, not a fix for a understood cause.
        Start-Sleep -Milliseconds 200
        [void](Send-WmCommandClick -WindowHandle $Dialog.Hwnd -Id $script:kOkButtonId -ControlHandle $Dialog.OkButton)
    }
    if (-not (Wait-WindowGone -Handle $Dialog.Hwnd -TimeoutMs $TimeoutMs)) {
        throw "Close-SkipSetupDialog: Skip Setup did not close within ${TimeoutMs}ms"
    }
}

function Get-SkipSetupRangeCount {
    [CmdletBinding()]
    param([Parameter(Mandatory)][PSCustomObject]$Dialog)
    return Get-ListViewItemCount -ListHandle $Dialog.RangeList
}

function Wait-SkipSetupRangeCount {
    <#
    .SYNOPSIS
    Polls Get-SkipSetupRangeCount until it reaches `Expected` or the
    timeout elapses, returning whatever the last read was either way.

    .DESCRIPTION
    Prefer this over a single Get-SkipSetupRangeCount call whenever the
    caller already knows what count to expect (which every precondition
    check in this suite does) - a single immediate read has been observed
    live to be genuinely, stably wrong (not just a one-frame flicker) for
    up to roughly a second after a fresh Open-SkipSetupDialog, apparently
    while PotPlayer's own WM_INITDIALOG is still populating the ListView.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][PSCustomObject]$Dialog,
        [Parameter(Mandatory)][int]$Expected,
        [int]$TimeoutMs = 2000,
        [int]$PollIntervalMs = 100
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    $count = Get-SkipSetupRangeCount -Dialog $Dialog
    while ($count -ne $Expected -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds $PollIntervalMs
        $count = Get-SkipSetupRangeCount -Dialog $Dialog
    }
    return $count
}

function Get-SkipSetupEnabled {
    [CmdletBinding()]
    param([Parameter(Mandatory)][PSCustomObject]$Dialog)
    return Get-CheckboxChecked -ControlHandle $Dialog.EnableCheckbox
}

function Set-SkipSetupEnabled {
    <#
    .SYNOPSIS
    Mirrors EnsureSkipEnabled (skip_setup.cpp): turns the checkbox on if
    it's off, and confirms the read-back, but never turns it off (PTS-012
    never exposes that as an automated action, only a human clicking the
    real checkbox does).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][PSCustomObject]$Dialog)

    if (Get-SkipSetupEnabled -Dialog $Dialog) { return $true }
    Invoke-CheckboxClick -ControlHandle $Dialog.EnableCheckbox
    Start-Sleep -Milliseconds 100
    return (Get-SkipSetupEnabled -Dialog $Dialog)
}

function Add-SkipSetupRange {
    <#
    .SYNOPSIS
    Full Add... -> Skip Interval Setup -> fill -> read back -> OK round
    trip for one File-specific range, mirroring AddFileSpecificSkipRange
    exactly (including its Cancel-on-mismatch, never-commit-unverified
    behavior).

    .OUTPUTS
    $true once the range list's count has gone up by exactly one. Throws on
    any failure along the way (Add not opening the dialog, a missing
    control, a read-back mismatch, or a count that didn't move by exactly
    one) - unlike the C++ original, which logs and returns false, this is
    test code where a hard failure should stop the test case outright.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][PSCustomObject]$Dialog,
        [Parameter(Mandatory)][int64]$StartMs,
        [Parameter(Mandatory)][int64]$EndMs,
        [int]$TimeoutMs = 3000
    )
    if ($EndMs -le $StartMs) { throw "Add-SkipSetupRange: end ($EndMs) must be after start ($StartMs)" }

    $countBefore = Get-SkipSetupRangeCount -Dialog $Dialog

    [void](Send-WmCommandClick -WindowHandle $Dialog.Hwnd -Id $script:kAddButtonId -ControlHandle $Dialog.AddButton)
    $interval = Find-DialogByTitle -Title $script:kSkipIntervalTitle -TimeoutMs $TimeoutMs
    if (-not $interval) { throw "Add-SkipSetupRange: Skip Interval Setup did not appear within ${TimeoutMs}ms" }

    $startEdit = Get-DlgItemHandle -DialogHandle $interval -Id $script:kIntervalStartEditId
    $endEdit = Get-DlgItemHandle -DialogHandle $interval -Id $script:kIntervalEndEditId
    $typeCombo = Get-DlgItemHandle -DialogHandle $interval -Id $script:kIntervalTypeComboId
    $okButton = Get-DlgItemHandle -DialogHandle $interval -Id $script:kIntervalOkButtonId
    $cancelButton = Get-DlgItemHandle -DialogHandle $interval -Id $script:kIntervalCancelButtonId
    if (-not ($startEdit -and $endEdit -and $typeCombo -and $okButton -and $cancelButton)) {
        [void](Send-WmCommandClick -WindowHandle $interval -Id $script:kIntervalCancelButtonId -ControlHandle $cancelButton)
        throw "Add-SkipSetupRange: Skip Interval Setup opened but a control was not found - layout mismatch (see docs/REVERIFICATION_CHECKLIST.md)"
    }

    $startText = Format-SkipTimecode -Ms $StartMs
    $endText = Format-SkipTimecode -Ms $EndMs
    Set-ControlText -ControlHandle $startEdit -Text $startText
    Set-ControlText -ControlHandle $endEdit -Text $endText
    Set-ComboSelection -ControlHandle $typeCombo -Index $script:kFileSpecificComboIndex

    $readStartText = Get-ControlText -ControlHandle $startEdit
    $readEndText = Get-ControlText -ControlHandle $endEdit
    $readType = Get-ComboSelection -ControlHandle $typeCombo

    $mismatch = $null
    try {
        if ((ConvertFrom-SkipTimecode -Text $readStartText) -ne $StartMs) { $mismatch = "start: expected '$startText', dialog reads '$readStartText'" }
        elseif ((ConvertFrom-SkipTimecode -Text $readEndText) -ne $EndMs) { $mismatch = "end: expected '$endText', dialog reads '$readEndText'" }
        elseif ($readType -ne $script:kFileSpecificComboIndex) { $mismatch = "type: expected $script:kFileSpecificComboIndex, dialog reads $readType" }
    } catch {
        $mismatch = "read-back text did not parse as a timecode: $($_.Exception.Message)"
    }

    if ($mismatch) {
        [void](Send-WmCommandClick -WindowHandle $interval -Id $script:kIntervalCancelButtonId -ControlHandle $cancelButton)
        [void](Wait-WindowGone -Handle $interval -TimeoutMs $TimeoutMs)
        throw "Add-SkipSetupRange: Skip Interval Setup read-back mismatch, cancelled rather than committed - $mismatch"
    }

    [void](Send-WmCommandClick -WindowHandle $interval -Id $script:kIntervalOkButtonId -ControlHandle $okButton)
    if (-not (Wait-WindowGone -Handle $interval -TimeoutMs $TimeoutMs)) {
        throw "Add-SkipSetupRange: Skip Interval Setup did not close within ${TimeoutMs}ms after OK"
    }

    $countAfter = Get-SkipSetupRangeCount -Dialog $Dialog
    if ($countAfter -ne $countBefore + 1) {
        throw "Add-SkipSetupRange: range list count went from $countBefore to $countAfter after Add (expected +1)"
    }
    return $true
}

function Remove-SkipSetupRangeAt {
    <#
    .SYNOPSIS
    Mirrors DeleteSkipRange: selects exactly `Index` (deselecting
    everything else first) and clicks Delete, waiting for the count to drop
    by exactly one.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][PSCustomObject]$Dialog,
        [Parameter(Mandatory)][int]$Index,
        [int]$TimeoutMs = 3000
    )

    $countBefore = Get-SkipSetupRangeCount -Dialog $Dialog
    if ($Index -lt 0 -or $Index -ge $countBefore) {
        throw "Remove-SkipSetupRangeAt: index $Index is out of range for a list of $countBefore"
    }

    Select-ListViewItem -ListHandle $Dialog.RangeList -Index -1 -State 0 -StateMask $script:LVIS_SELECTED
    Select-ListViewItem -ListHandle $Dialog.RangeList -Index $Index `
        -State ($script:LVIS_SELECTED -bor $script:LVIS_FOCUSED) -StateMask ($script:LVIS_SELECTED -bor $script:LVIS_FOCUSED)

    [void](Send-WmCommandClick -WindowHandle $Dialog.Hwnd -Id $script:kDeleteButtonId -ControlHandle $Dialog.DeleteButton)
    if (-not (Wait-ListViewItemCount -ListHandle $Dialog.RangeList -ExpectedCount ($countBefore - 1) -TimeoutMs $TimeoutMs)) {
        throw "Remove-SkipSetupRangeAt: range list count did not drop to $($countBefore - 1) within ${TimeoutMs}ms after Delete"
    }
}

function Clear-SkipSetupRanges {
    <#
    .SYNOPSIS
    Mirrors ClearAllSkipRanges/RunClearAllLoop: repeatedly deletes row 0
    until the list is empty. No-op on an already-empty list. Bounded by the
    starting count, same as the C++ original, so a count that doesn't shrink
    as expected can't spin forever.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][PSCustomObject]$Dialog, [int]$TimeoutMs = 3000)

    $initialCount = Get-SkipSetupRangeCount -Dialog $Dialog
    for ($i = 0; $i -lt $initialCount; $i++) {
        Remove-SkipSetupRangeAt -Dialog $Dialog -Index 0 -TimeoutMs $TimeoutMs
        if ((Get-SkipSetupRangeCount -Dialog $Dialog) -eq 0) { return }
    }
    $remaining = Get-SkipSetupRangeCount -Dialog $Dialog
    if ($remaining -ne 0) {
        throw "Clear-SkipSetupRanges: $remaining range(s) still remain after $initialCount delete attempts"
    }
}

Export-ModuleMember -Function `
    Format-SkipTimecode, `
    ConvertFrom-SkipTimecode, `
    Find-PotPlayerMainWindow, `
    Open-SkipSetupDialog, `
    Close-SkipSetupDialog, `
    Get-SkipSetupRangeCount, `
    Wait-SkipSetupRangeCount, `
    Get-SkipSetupEnabled, `
    Set-SkipSetupEnabled, `
    Add-SkipSetupRange, `
    Remove-SkipSetupRangeAt, `
    Clear-SkipSetupRanges
