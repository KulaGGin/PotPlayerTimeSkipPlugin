# Win32Interop.psm1  -  PTS-018's cross-process Win32 plumbing.
#
# The out-of-process counterpart to plugin/src/skip_setup.cpp and
# plugin/src/player_window.cpp: the same PostMessage/SendMessage techniques
# docs/FINDINGS.md sections 3/6 measured for the pre-PTS-010 cross-process
# prototype, reused here because a *test harness* has to observe and drive
# PotPlayer from outside its process, unlike the shipped plugin (which runs
# inside it). Nothing in this file is unit-testable without a live window to
# point it at  -  see PbfCodec.Tests.ps1 for the part of this suite that is.
#
# Cross-process gotchas this module exists to route around correctly
# (docs/FINDINGS.md section 6, all independently measured there):
#   - GetWindowText returns empty across a process boundary; only WM_GETTEXT
#     (via SendMessage) is marshalled. Same for WM_SETTEXT.
#   - A button whose click handler pops a modal DialogBox (Add..., OK,
#     Cancel) must be driven with PostMessage, never SendMessage  -  a
#     SendMessage there blocks this process until that modal dialog closes.
#   - LVM_SETITEMSTATE's lParam is a pointer to an LVITEM struct that must
#     live in the *target* process's address space  -  WriteProcessMemory
#     into a remote allocation, not a local struct's address.

Set-StrictMode -Version Latest

if (-not ('PotPlayerTimeSkip.Win32' -as [type])) {
    Add-Type -Namespace PotPlayerTimeSkip -Name Win32 -UsingNamespace System.Text -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

[DllImport("user32.dll", SetLastError = true)]
public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

[DllImport("user32.dll", SetLastError = true)]
public static extern bool IsWindow(IntPtr hWnd);

[DllImport("user32.dll", SetLastError = true)]
public static extern bool IsWindowVisible(IntPtr hWnd);

[DllImport("user32.dll", SetLastError = true)]
public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

[DllImport("user32.dll", SetLastError = true)]
public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
public static extern IntPtr SendMessageText(IntPtr hWnd, uint msg, IntPtr wParam, StringBuilder lParam);

[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
public static extern IntPtr SendMessageSetText(IntPtr hWnd, uint msg, IntPtr wParam, string lParam);

[DllImport("user32.dll", SetLastError = true, EntryPoint = "SendMessageW")]
public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

[DllImport("user32.dll", SetLastError = true)]
public static extern IntPtr GetForegroundWindow();

[DllImport("user32.dll", SetLastError = true)]
public static extern bool SetForegroundWindow(IntPtr hWnd);

[DllImport("user32.dll", SetLastError = true)]
public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

[DllImport("user32.dll", SetLastError = true)]
public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern uint GetCurrentThreadId();

[DllImport("user32.dll", SetLastError = true)]
public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool CloseHandle(IntPtr hObject);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint flAllocationType, uint flProtect);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint dwFreeType);

[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, UIntPtr nSize, out UIntPtr lpNumberOfBytesWritten);

[StructLayout(LayoutKind.Sequential)]
public struct INPUT {
    public uint type;
    public InputUnion U;
}

[StructLayout(LayoutKind.Explicit)]
public struct InputUnion {
    [FieldOffset(0)] public KEYBDINPUT ki;
}

[StructLayout(LayoutKind.Sequential)]
public struct KEYBDINPUT {
    public ushort wVk;
    public ushort wScan;
    public uint dwFlags;
    public uint time;
    public IntPtr dwExtraInfo;
}

[DllImport("user32.dll", SetLastError = true)]
public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
'@
}

# WM_* / BM_* / CB_* / LVM_* constants (winuser.h / commctrl.h), spelled
# numerically like plugin/hotkeys.hpp does for the same reason: no SDK
# header, just the documented values.
$script:WM_COMMAND = 0x0111
$script:WM_GETTEXT = 0x000D
$script:WM_SETTEXT = 0x000C
$script:BM_GETCHECK = 0x00F0
$script:BM_CLICK = 0x00F5
$script:BST_CHECKED = 1
$script:CB_GETCURSEL = 0x0147
$script:CB_SETCURSEL = 0x014E
$script:LVM_GETITEMCOUNT = 0x1004
$script:LVM_SETITEMSTATE = 0x102B
$script:LVIS_SELECTED = 0x0002
$script:LVIS_FOCUSED = 0x0001
$script:LVIF_STATE = 0x0008
$script:BN_CLICKED = 0
$script:MEM_COMMIT = 0x1000
$script:MEM_RESERVE = 0x2000
$script:MEM_RELEASE = 0x8000
$script:PAGE_READWRITE = 0x04
$script:PROCESS_VM_OPERATION = 0x0008
$script:PROCESS_VM_READ = 0x0010
$script:PROCESS_VM_WRITE = 0x0020
$script:PROCESS_QUERY_INFORMATION = 0x0400
$script:INPUT_KEYBOARD = 1
$script:KEYEVENTF_KEYUP = 0x0002
$script:VK_MENU = 0x12    # Alt

function Find-WindowByClass {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ClassName)
    $h = [PotPlayerTimeSkip.Win32]::FindWindow($ClassName, $null)
    if ($h -eq [IntPtr]::Zero) { return $null }
    return $h
}

function Get-TopLevelWindows {
    <#
    .SYNOPSIS
    Enumerates every top-level window system-wide, returning class/title/handle.
    Mirrors plugin/window_enum.hpp's EnumerateOwnProcessWindows, minus the
    own-process filter  -  a cross-process harness has no "own process" to
    scope to.
    #>
    [CmdletBinding()]
    param()

    $results = New-Object System.Collections.Generic.List[object]
    $callback = {
        param([IntPtr]$hWnd, [IntPtr]$lParam)
        $classBuf = New-Object System.Text.StringBuilder 256
        [void][PotPlayerTimeSkip.Win32]::GetClassName($hWnd, $classBuf, $classBuf.Capacity)
        $titleBuf = New-Object System.Text.StringBuilder 256
        [void][PotPlayerTimeSkip.Win32]::GetWindowText($hWnd, $titleBuf, $titleBuf.Capacity)
        $results.Add([PSCustomObject]@{
            Handle    = $hWnd
            ClassName = $classBuf.ToString()
            Title     = $titleBuf.ToString()
        })
        return $true
    } -as [PotPlayerTimeSkip.Win32+EnumWindowsProc]

    [void][PotPlayerTimeSkip.Win32]::EnumWindows($callback, [IntPtr]::Zero)
    return ,$results.ToArray()
}

function Find-DialogByTitle {
    <#
    .SYNOPSIS
    Polls Get-TopLevelWindows for a "#32770" dialog with the given exact
    title, the same technique plugin/src/skip_setup.cpp's FindAndParkFallback
    uses in-process. Cross-process has no CBT-hook fast path (PTS-010's hook
    is a same-process, same-thread mechanism), so this is deliberately just
    the fallback path, always  -  the dialog WILL be visible for a moment,
    which is fine for a test harness (unlike the shipped plugin, whose whole
    point is never showing it).

    .OUTPUTS
    The dialog's HWND, or $null if it didn't appear within TimeoutMs.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Title,
        [int]$TimeoutMs = 3000,
        [int]$PollIntervalMs = 25
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        foreach ($w in (Get-TopLevelWindows)) {
            if ($w.ClassName -eq '#32770' -and $w.Title -eq $Title) {
                return $w.Handle
            }
        }
        Start-Sleep -Milliseconds $PollIntervalMs
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Wait-WindowGone {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][IntPtr]$Handle,
        [int]$TimeoutMs = 3000,
        [int]$PollIntervalMs = 25
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        if (-not [PotPlayerTimeSkip.Win32]::IsWindow($Handle)) { return $true }
        Start-Sleep -Milliseconds $PollIntervalMs
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Get-DlgItemHandle {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$DialogHandle, [Parameter(Mandatory)][int]$Id)
    $h = [PotPlayerTimeSkip.Win32]::GetDlgItem($DialogHandle, $Id)
    if ($h -eq [IntPtr]::Zero) { return $null }
    return $h
}

function Send-WmCommandClick {
    <#
    .SYNOPSIS
    PostMessage(WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), controlHandle)  -
    never SendMessage, matching skip_setup.cpp's own reasoning: the target's
    handler may run its own modal DialogBox loop (Add..., OK, Cancel), which
    would otherwise block this process for as long as that dialog stays open.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][IntPtr]$WindowHandle,
        [Parameter(Mandatory)][int]$Id,
        [Parameter(Mandatory)][IntPtr]$ControlHandle
    )
    $wParam = [IntPtr]([int]$Id -bor ($script:BN_CLICKED -shl 16))
    return [PotPlayerTimeSkip.Win32]::PostMessage($WindowHandle, $script:WM_COMMAND, $wParam, $ControlHandle)
}

function Get-ControlText {
    <#
    .SYNOPSIS
    WM_GETTEXT via SendMessage  -  the one cross-process-marshalled way to
    read another process's control text (docs/FINDINGS.md section 6;
    GetWindowText alone returns empty here).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle, [int]$MaxLength = 128)
    $buf = New-Object System.Text.StringBuilder $MaxLength
    [void][PotPlayerTimeSkip.Win32]::SendMessageText($ControlHandle, $script:WM_GETTEXT, [IntPtr]$MaxLength, $buf)
    return $buf.ToString()
}

function Set-ControlText {
    <#
    .SYNOPSIS
    WM_SETTEXT via SendMessage  -  same auto-marshalled-message family as
    WM_GETTEXT, safe to SendMessage (not a button whose handler pops a modal
    dialog).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle, [Parameter(Mandatory)][string]$Text)
    [void][PotPlayerTimeSkip.Win32]::SendMessageSetText($ControlHandle, $script:WM_SETTEXT, [IntPtr]::Zero, $Text)
}

function Get-ComboSelection {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle)
    return [int][PotPlayerTimeSkip.Win32]::SendMessage($ControlHandle, $script:CB_GETCURSEL, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Set-ComboSelection {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle, [Parameter(Mandatory)][int]$Index)
    [void][PotPlayerTimeSkip.Win32]::SendMessage($ControlHandle, $script:CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
}

function Get-CheckboxChecked {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle)
    $state = [PotPlayerTimeSkip.Win32]::SendMessage($ControlHandle, $script:BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero)
    return ([int]$state -eq $script:BST_CHECKED)
}

function Invoke-CheckboxClick {
    <#
    .SYNOPSIS
    BM_CLICK via SendMessage  -  safe here (unlike Add.../OK/Cancel): a
    plain checkbox's handler doesn't pop a modal dialog, matching
    EnsureSkipEnabled's own choice in skip_setup.cpp.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ControlHandle)
    [void][PotPlayerTimeSkip.Win32]::SendMessage($ControlHandle, $script:BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Get-ListViewItemCount {
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$ListHandle)
    return [int][PotPlayerTimeSkip.Win32]::SendMessage($ListHandle, $script:LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Wait-ListViewItemCount {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][IntPtr]$ListHandle,
        [Parameter(Mandatory)][int]$ExpectedCount,
        [int]$TimeoutMs = 3000,
        [int]$PollIntervalMs = 25
    )
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    do {
        if ((Get-ListViewItemCount -ListHandle $ListHandle) -eq $ExpectedCount) { return $true }
        Start-Sleep -Milliseconds $PollIntervalMs
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Select-ListViewItem {
    <#
    .SYNOPSIS
    LVM_SETITEMSTATE, selecting exactly one row (deselecting the rest first
    via iItem = -1), mirroring DeleteSkipRange's own ListView_SetItemState
    pair in skip_setup.cpp. Requires WriteProcessMemory because the LVITEM
    struct LVM_SETITEMSTATE's lParam points to must live in the *target*
    process's address space, not this script's  -  a plain local struct's
    address is meaningless across the process boundary (unlike WM_GETTEXT/
    WM_SETTEXT, which the OS marshals for a short, fixed list of legacy
    messages; LVM_SETITEMSTATE isn't one of them).

    .PARAMETER Index
    -1 clears every item's selected state (LVIS_SELECTED only, no FOCUSED)
    before selecting `Index` proper  -  call this twice, like
    DeleteSkipRange does, not once with a combined mask.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][IntPtr]$ListHandle,
        [Parameter(Mandatory)][int]$Index,
        [Parameter(Mandatory)][uint32]$State,
        [Parameter(Mandatory)][uint32]$StateMask
    )

    $ownerPid = 0
    [void][PotPlayerTimeSkip.Win32]::GetWindowThreadProcessId($ListHandle, [ref]$ownerPid)
    if ($ownerPid -eq 0) { throw "Select-ListViewItem: could not resolve the range list's owning process id" }

    $access = $script:PROCESS_VM_OPERATION -bor $script:PROCESS_VM_WRITE -bor $script:PROCESS_VM_READ -bor $script:PROCESS_QUERY_INFORMATION
    $hProcess = [PotPlayerTimeSkip.Win32]::OpenProcess($access, $false, $ownerPid)
    if ($hProcess -eq [IntPtr]::Zero) { throw "Select-ListViewItem: OpenProcess failed for pid $ownerPid, gle=$([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

    try {
        # LVITEMW layout (x64): mask@0, iItem@4, iSubItem@8, state@12,
        # stateMask@16, then pointer/other fields this call never touches.
        # 96 bytes covers the full struct with room to spare; only the
        # first 20 bytes are ever meaningfully written since mask =
        # LVIF_STATE (state-only update).
        $bufSize = 96
        $remote = [PotPlayerTimeSkip.Win32]::VirtualAllocEx($hProcess, [IntPtr]::Zero, [UIntPtr]$bufSize, ($script:MEM_COMMIT -bor $script:MEM_RESERVE), $script:PAGE_READWRITE)
        if ($remote -eq [IntPtr]::Zero) { throw "Select-ListViewItem: VirtualAllocEx failed, gle=$([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

        try {
            $buf = New-Object byte[] $bufSize
            [BitConverter]::GetBytes([uint32]$script:LVIF_STATE).CopyTo($buf, 0)
            [BitConverter]::GetBytes([int32]$Index).CopyTo($buf, 4)
            [BitConverter]::GetBytes([int32]0).CopyTo($buf, 8)
            [BitConverter]::GetBytes([uint32]$State).CopyTo($buf, 12)
            [BitConverter]::GetBytes([uint32]$StateMask).CopyTo($buf, 16)

            $written = [UIntPtr]::Zero
            $ok = [PotPlayerTimeSkip.Win32]::WriteProcessMemory($hProcess, $remote, $buf, [UIntPtr]$bufSize, [ref]$written)
            if (-not $ok) { throw "Select-ListViewItem: WriteProcessMemory failed, gle=$([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

            [void][PotPlayerTimeSkip.Win32]::SendMessage($ListHandle, $script:LVM_SETITEMSTATE, [IntPtr](-1), $remote)
        } finally {
            [void][PotPlayerTimeSkip.Win32]::VirtualFreeEx($hProcess, $remote, [UIntPtr]0, $script:MEM_RELEASE)
        }
    } finally {
        [void][PotPlayerTimeSkip.Win32]::CloseHandle($hProcess)
    }
}

function Get-ForegroundWindowHandle {
    [CmdletBinding()]
    param()
    return [PotPlayerTimeSkip.Win32]::GetForegroundWindow()
}

function Set-ForegroundWindowSafe {
    <#
    .SYNOPSIS
    Best-effort SetForegroundWindow from a background script process.

    .DESCRIPTION
    Windows' foreground-lock (docs/FINDINGS.md section 6) blocks a plain
    SetForegroundWindow call from a process that isn't already foreground.
    This uses the standard workaround  -  AttachThreadInput to the thread
    that currently owns the foreground window, which is what "attached
    input queues share one foreground/focus state" is documented to permit
    -  and reports whether the target actually ended up foreground
    afterward rather than trusting the API call's own return value (which
    can report success without the window really having moved to front).

    .OUTPUTS
    $true if `Handle` is confirmed foreground afterward, $false otherwise
    (logged by the caller  -  this never throws, since "couldn't force
    foreground" is a recoverable precondition failure, not a bug).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][IntPtr]$Handle)

    if ((Get-ForegroundWindowHandle) -eq $Handle) { return $true }

    $currentForeground = [PotPlayerTimeSkip.Win32]::GetForegroundWindow()
    $foreThreadId = 0
    [void][PotPlayerTimeSkip.Win32]::GetWindowThreadProcessId($currentForeground, [ref]$foreThreadId)
    $ourThreadId = [PotPlayerTimeSkip.Win32]::GetCurrentThreadId()

    $attached = $false
    if ($foreThreadId -ne 0 -and $foreThreadId -ne $ourThreadId) {
        $attached = [PotPlayerTimeSkip.Win32]::AttachThreadInput($ourThreadId, $foreThreadId, $true)
    }
    try {
        [void][PotPlayerTimeSkip.Win32]::SetForegroundWindow($Handle)
    } finally {
        if ($attached) {
            [void][PotPlayerTimeSkip.Win32]::AttachThreadInput($ourThreadId, $foreThreadId, $false)
        }
    }

    Start-Sleep -Milliseconds 50
    return ((Get-ForegroundWindowHandle) -eq $Handle)
}

function Send-AltChordKey {
    <#
    .SYNOPSIS
    Synthesizes a real Alt+<key> chord via SendInput  -  "the real
    three-key gesture" the issue asks for, not a call into plugin internals.
    Reaches plugin::RunHotkeyPump's RegisterHotKey registration exactly like
    a physical keypress would, including its foreground gate
    (IsPotPlayerForeground in plugin/src/hotkey_pump.cpp)  -  the caller is
    responsible for making PotPlayer foreground first (Set-ForegroundWindowSafe)
    unless the plugin's config.ini has [Hotkeys] Global=true.

    .PARAMETER VirtualKey
    A VK_* code, e.g. 0x41 for 'A', 0xDB for '[', 0xDD for ']'  -  the same
    constants plugin/include/plugin/hotkeys.hpp defines (kVkA/kVkOpenBracket/
    kVkCloseBracket).
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][uint16]$VirtualKey)

    $KEYEVENTF_KEYUP = $script:KEYEVENTF_KEYUP
    $inputs = New-Object 'PotPlayerTimeSkip.Win32+INPUT[]' 4

    $inputs[0] = New-Object PotPlayerTimeSkip.Win32+INPUT
    $inputs[0].type = $script:INPUT_KEYBOARD
    $inputs[0].U.ki.wVk = $script:VK_MENU

    $inputs[1] = New-Object PotPlayerTimeSkip.Win32+INPUT
    $inputs[1].type = $script:INPUT_KEYBOARD
    $inputs[1].U.ki.wVk = $VirtualKey

    $inputs[2] = New-Object PotPlayerTimeSkip.Win32+INPUT
    $inputs[2].type = $script:INPUT_KEYBOARD
    $inputs[2].U.ki.wVk = $VirtualKey
    $inputs[2].U.ki.dwFlags = $KEYEVENTF_KEYUP

    $inputs[3] = New-Object PotPlayerTimeSkip.Win32+INPUT
    $inputs[3].type = $script:INPUT_KEYBOARD
    $inputs[3].U.ki.wVk = $script:VK_MENU
    $inputs[3].U.ki.dwFlags = $KEYEVENTF_KEYUP

    $size = [System.Runtime.InteropServices.Marshal]::SizeOf([type]'PotPlayerTimeSkip.Win32+INPUT')
    $sent = [PotPlayerTimeSkip.Win32]::SendInput(4, $inputs, $size)
    if ($sent -ne 4) {
        throw "Send-AltChordKey: SendInput only accepted $sent of 4 events, gle=$([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
}

Export-ModuleMember -Function `
    Find-WindowByClass, `
    Get-TopLevelWindows, `
    Find-DialogByTitle, `
    Wait-WindowGone, `
    Get-DlgItemHandle, `
    Send-WmCommandClick, `
    Get-ControlText, `
    Set-ControlText, `
    Get-ComboSelection, `
    Set-ComboSelection, `
    Get-CheckboxChecked, `
    Invoke-CheckboxClick, `
    Get-ListViewItemCount, `
    Wait-ListViewItemCount, `
    Select-ListViewItem, `
    Get-ForegroundWindowHandle, `
    Set-ForegroundWindowSafe, `
    Send-AltChordKey `
    -Variable `
    WM_COMMAND, WM_GETTEXT, WM_SETTEXT, BM_GETCHECK, BM_CLICK, BST_CHECKED, `
    CB_GETCURSEL, CB_SETCURSEL, LVM_GETITEMCOUNT, LVM_SETITEMSTATE, `
    LVIS_SELECTED, LVIS_FOCUSED, LVIF_STATE, BN_CLICKED
