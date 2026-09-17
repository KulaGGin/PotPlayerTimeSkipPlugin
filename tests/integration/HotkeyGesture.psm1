# HotkeyGesture.psm1  -  PTS-018's "drives the real three-key gesture" half
# of the issue.
#
# Sends actual Alt+[ / Alt+] / Alt+A key chords via SendInput, reaching
# plugin::RunHotkeyPump's RegisterHotKey registration and, through it,
# SkipMarkingStateMachine (plugin/src/skip_marking.cpp) exactly the way a
# real keypress would  -  unlike SkipSetupAutomation.psm1, which bypasses
# the hotkey/state-machine layer entirely and drives the dialogs directly.
#
# Deliberately does NOT attempt to control the resulting timestamps
# precisely: OnAltOpenBracket/OnAltCloseBracket capture whatever
# plugin::GetPositionMs() reports at the instant each key is handled
# (plugin/src/skip_marking.cpp), and this module has no supported way to
# seek playback to an exact position (no such primitive is exposed anywhere
# in this codebase - see docs/FINDINGS.md section 4's playback-query-only
# WM_USER table). Run-IntegrationTests.ps1's hotkey-gesture test therefore
# only asserts the *shape* of what happened (a range appeared, its start is
# less than its end, the position moved between the two marks), never an
# exact millisecond value - that precision is what the dialog-driven tests
# in SkipSetupAutomation.psm1 are for.

Set-StrictMode -Version Latest

# No -Force: see SkipSetupAutomation.psm1's own comment on this same line -
# a nested Import-Module -Force on an already-loaded dependency strips its
# exports back out of the caller's global session.
Import-Module (Join-Path $PSScriptRoot 'Win32Interop.psm1')

# plugin/include/plugin/hotkeys.hpp's VK_* constants (US layout).
$script:kVkA = 0x41
$script:kVkOpenBracket = 0xDB
$script:kVkCloseBracket = 0xDD

# WM_HOTKEY is queued and drained on plugin::RunHotkeyPump's own message
# loop (MsgWaitForMultipleObjects, plugin/src/hotkey_pump.cpp) - this is
# just slack for that round trip plus OnAlt*'s own work (OnAltA's commitRange
# drives a full dialog round trip), not a documented timing guarantee.
$script:kDispatchSettleMs = 150

function Send-MarkStartHotkey {
    <#
    .SYNOPSIS
    Alt+[  -  sets the pending mark's start to the current playback position.
    #>
    [CmdletBinding()]
    param()
    Send-AltChordKey -VirtualKey $script:kVkOpenBracket
    Start-Sleep -Milliseconds $script:kDispatchSettleMs
}

function Send-MarkEndHotkey {
    <#
    .SYNOPSIS
    Alt+]  -  sets the pending mark's end to the current playback position.
    #>
    [CmdletBinding()]
    param()
    Send-AltChordKey -VirtualKey $script:kVkCloseBracket
    Start-Sleep -Milliseconds $script:kDispatchSettleMs
}

function Send-CommitMarkHotkey {
    <#
    .SYNOPSIS
    Alt+A  -  commits the pending start/end as one new range. This is the
    one key whose handler drives a full Skip Setup round trip
    (CommitRangeLive, plugin/src/skip_marking.cpp), so it needs more settle
    time than a plain pending-mark update.
    #>
    [CmdletBinding()]
    param([int]$SettleMs = 2000)
    Send-AltChordKey -VirtualKey $script:kVkA
    Start-Sleep -Milliseconds $SettleMs
}

Export-ModuleMember -Function Send-MarkStartHotkey, Send-MarkEndHotkey, Send-CommitMarkHotkey
