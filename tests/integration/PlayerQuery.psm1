# PlayerQuery.psm1  -  cross-process twin of plugin/src/player_window.cpp's
# playback-state queries (PTS-009), used here for test preconditions and
# for the hotkey-gesture test's before/after position checks (PTS-018).
#
# Same WM_USER (0x0400) codes docs/FINDINGS.md section 4 measured, sent via
# plain cross-process SendMessage - no marshalling concerns here (unlike
# WM_GETTEXT/WM_SETTEXT or LVM_SETITEMSTATE in Win32Interop.psm1): these are
# fixed-size integer replies, not pointers into either process's memory.

Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot 'Win32Interop.psm1') -Force

$script:WM_USER = 0x0400
$script:kPositionQuery = 0x5004
$script:kDurationQuery = 0x5002
$script:kStatusQuery = 0x5006

function Get-PlaybackPositionMs {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$MainWindow)
    return [int64](Send-RawUserMessage -WindowHandle $MainWindow -WParam $script:kPositionQuery)
}

function Get-PlaybackDurationMs {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$MainWindow)
    return [int64](Send-RawUserMessage -WindowHandle $MainWindow -WParam $script:kDurationQuery)
}

function Test-PotPlayerFileOpen {
    <#
    .SYNOPSIS
    Mirrors plugin::IsFileOpen: duration == 0 is how PotPlayer represents
    "no file open" (docs/FINDINGS.md section 4), distinct from genuinely
    sitting at 0:00 in an open file (where duration is > 0).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$MainWindow)
    return (Get-PlaybackDurationMs -MainWindow $MainWindow) -gt 0
}

function Send-RawUserMessage {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$WindowHandle, [Parameter(Mandatory)][int]$WParam)
    return [PotPlayerTimeSkip.Win32]::SendMessage($WindowHandle, $script:WM_USER, [IntPtr]$WParam, [IntPtr]::Zero)
}

Export-ModuleMember -Function Get-PlaybackPositionMs, Get-PlaybackDurationMs, Test-PotPlayerFileOpen
