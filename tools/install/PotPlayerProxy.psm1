# PotPlayerProxy.psm1  -  PTS-007 install/uninstall/status/repair logic.
#
# Dependency-free by design (no dumpbin, no external modules): PE export
# tables are parsed directly from the file bytes so this works on a machine
# that only has PowerShell, which is also what makes it testable without a
# real PotPlayer install (see Tests.ps1).
#
# Safety model: every mutating operation only ever adds or renames files  - 
# nothing is ever deleted until a replacement is already safely in place
# (see Install-PotPlayerProxy / Uninstall-PotPlayerProxy for the exact
# rename-then-rename-back sequencing), and any state this module doesn't
# recognize is reported as 'Unknown' rather than guessed at.

Set-StrictMode -Version Latest

function Get-PEExportInfo {
    <#
    .SYNOPSIS
    Parses a PE (x64) file's export directory without any external tools.

    .OUTPUTS
    PSCustomObject with:
      Path    - the file that was parsed
      DllName - the name embedded in the export directory (may be null)
      Exports - hashtable: exported name -> forwarder target string
                ("OtherDll.OtherFunc"), or $null if it's a real (non-forwarded)
                export.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Path
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)

    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a valid PE file (missing MZ signature): $Path"
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -le 0 -or ($peOffset + 24) -gt $bytes.Length) {
        throw "Not a valid PE file (bad e_lfanew): $Path"
    }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Not a valid PE file (missing PE signature): $Path"
    }

    $fileHeaderOffset = $peOffset + 4
    $numberOfSections = [BitConverter]::ToUInt16($bytes, $fileHeaderOffset + 2)
    $sizeOfOptionalHeader = [BitConverter]::ToUInt16($bytes, $fileHeaderOffset + 16)

    $optionalHeaderOffset = $fileHeaderOffset + 20
    if ($sizeOfOptionalHeader -lt 120) {
        throw "PE optional header too small to contain an export data directory: $Path"
    }
    $magic = [BitConverter]::ToUInt16($bytes, $optionalHeaderOffset)
    if ($magic -ne 0x20B) {
        throw ("Not a PE32+ (x64) image (magic=0x{0:X4}): {1}" -f $magic, $Path)
    }

    $dataDirOffset = $optionalHeaderOffset + 112
    $exportRva = [BitConverter]::ToUInt32($bytes, $dataDirOffset)
    $exportSize = [BitConverter]::ToUInt32($bytes, $dataDirOffset + 4)

    if ($exportRva -eq 0) {
        return [PSCustomObject]@{ Path = $Path; DllName = $null; Exports = @{} }
    }

    $sectionHeadersOffset = $optionalHeaderOffset + $sizeOfOptionalHeader
    $sections = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $numberOfSections; $i++) {
        $so = $sectionHeadersOffset + ($i * 40)
        $sections.Add([PSCustomObject]@{
            VirtualAddress   = [BitConverter]::ToUInt32($bytes, $so + 12)
            VirtualSize      = [BitConverter]::ToUInt32($bytes, $so + 8)
            PointerToRawData = [BitConverter]::ToUInt32($bytes, $so + 20)
            SizeOfRawData    = [BitConverter]::ToUInt32($bytes, $so + 16)
        })
    }

    function local:ConvertRvaToOffset([uint32]$rva) {
        foreach ($s in $sections) {
            $size = [Math]::Max($s.VirtualSize, $s.SizeOfRawData)
            if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + $size)) {
                return [int]($rva - $s.VirtualAddress + $s.PointerToRawData)
            }
        }
        throw ("RVA 0x{0:X} not found in any section of {1}" -f $rva, $Path)
    }

    function local:ReadAsciiZ([int]$offset) {
        $end = $offset
        while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
        return [System.Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
    }

    $exportDirOffset = ConvertRvaToOffset $exportRva
    $nameRva = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 12)
    $numberOfFunctions = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 20)
    $numberOfNames = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 24)
    $addrFunctionsRva = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 28)
    $addrNamesRva = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 32)
    $addrOrdinalsRva = [BitConverter]::ToUInt32($bytes, $exportDirOffset + 36)

    $dllName = $null
    if ($nameRva -ne 0) { $dllName = ReadAsciiZ (ConvertRvaToOffset $nameRva) }

    $functionsOffset = ConvertRvaToOffset $addrFunctionsRva
    $namesOffset = ConvertRvaToOffset $addrNamesRva
    $ordinalsOffset = ConvertRvaToOffset $addrOrdinalsRva

    $exports = @{}
    for ($i = 0; $i -lt $numberOfNames; $i++) {
        $nameEntryRva = [BitConverter]::ToUInt32($bytes, $namesOffset + ($i * 4))
        $name = ReadAsciiZ (ConvertRvaToOffset $nameEntryRva)
        $ordIndex = [BitConverter]::ToUInt16($bytes, $ordinalsOffset + ($i * 2))
        if ($ordIndex -ge $numberOfFunctions) {
            throw "Corrupt export table in $Path`: name '$name' has an out-of-range ordinal index"
        }
        $funcRva = [BitConverter]::ToUInt32($bytes, $functionsOffset + ($ordIndex * 4))

        $forwarderTarget = $null
        if ($funcRva -ge $exportRva -and $funcRva -lt ($exportRva + $exportSize)) {
            $forwarderTarget = ReadAsciiZ (ConvertRvaToOffset $funcRva)
        }

        $exports[$name] = $forwarderTarget
    }

    return [PSCustomObject]@{ Path = $Path; DllName = $dllName; Exports = $exports }
}

function Test-ForwarderSetMatches {
    <#
    .SYNOPSIS
    Checks that every export the proxy forwards resolves to a name the
    candidate "original" DLL actually exports.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][hashtable]$ProxyExports,
        [Parameter(Mandatory)][hashtable]$OriginalExports
    )

    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($name in $ProxyExports.Keys) {
        $target = $ProxyExports[$name]
        if (-not $target) {
            $missing.Add("$name (proxy does not forward this export)")
            continue
        }
        $dotIndex = $target.LastIndexOf('.')
        if ($dotIndex -lt 0) {
            $missing.Add("$name (unparseable forwarder target '$target')")
            continue
        }
        $targetFunc = $target.Substring($dotIndex + 1)
        if (-not $OriginalExports.ContainsKey($targetFunc)) {
            $missing.Add("$name -> $targetFunc (not exported by the candidate original DLL)")
        }
    }

    return [PSCustomObject]@{
        Matches        = ($missing.Count -eq 0)
        MissingDetails = $missing
    }
}

function Test-DirectoryWritable {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
    $probe = Join-Path $Path (".write_test_{0}.tmp" -f ([guid]::NewGuid().ToString('N')))
    try {
        [System.IO.File]::WriteAllBytes($probe, [byte[]]@(0))
        return $true
    } catch {
        return $false
    } finally {
        if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue }
    }
}

function Test-PotPlayerRunning {
    [CmdletBinding()]
    param()
    Get-Process -Name 'PotPlayerMini64', 'PotPlayer64' -ErrorAction SilentlyContinue
}

function Get-PotPlayerVersion {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$InstallDir)

    $exe = Join-Path $InstallDir 'PotPlayerMini64.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return $null }
    $vi = (Get-Item -LiteralPath $exe).VersionInfo
    if ($vi.FileVersion) { return $vi.FileVersion }
    return $vi.ProductVersion
}

function Get-ProxyInstallState {
    <#
    .SYNOPSIS
    Classifies the install directory into one state, never guessing when
    the layout is ambiguous.

    .OUTPUTS
    PSCustomObject { State, LiveDll, OrigDll, Detail }, where State is one of:
      NotInstalled   - a real (non-forwarding) MediaDB64.dll, no backup. Pristine.
      Installed      - MediaDB64.dll forwards to MediaDB64_orig.dll, which exists.
      UpdateDetected - a real MediaDB64.dll sits next to a stale MediaDB64_orig.dll
                       backup (looks like PotPlayer was updated over the proxy).
      Missing        - neither file is present.
      Unknown        - anything else; refuse to touch it.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$InstallDir)

    $liveDll = Join-Path $InstallDir 'MediaDB64.dll'
    $origDll = Join-Path $InstallDir 'MediaDB64_orig.dll'
    $liveExists = Test-Path -LiteralPath $liveDll -PathType Leaf
    $origExists = Test-Path -LiteralPath $origDll -PathType Leaf

    if (-not $liveExists -and -not $origExists) {
        return [PSCustomObject]@{
            State = 'Missing'; LiveDll = $liveDll; OrigDll = $origDll
            Detail = "Neither MediaDB64.dll nor MediaDB64_orig.dll found in '$InstallDir'."
        }
    }
    if (-not $liveExists -and $origExists) {
        return [PSCustomObject]@{
            State = 'Unknown'; LiveDll = $liveDll; OrigDll = $origDll
            Detail = "MediaDB64_orig.dll exists but MediaDB64.dll is missing  -  the PotPlayer install looks broken. Stop and investigate manually."
        }
    }

    $isForwarder = $false
    try {
        $liveInfo = Get-PEExportInfo -Path $liveDll
        $isForwarder = ($liveInfo.Exports.Values | Where-Object { $_ } | Measure-Object).Count -gt 0
    } catch {
        return [PSCustomObject]@{
            State = 'Unknown'; LiveDll = $liveDll; OrigDll = $origDll
            Detail = "Could not parse MediaDB64.dll's export table: $($_.Exception.Message)"
        }
    }

    if (-not $origExists) {
        if ($isForwarder) {
            return [PSCustomObject]@{
                State = 'Unknown'; LiveDll = $liveDll; OrigDll = $origDll
                Detail = "MediaDB64.dll forwards exports but MediaDB64_orig.dll is missing  -  nothing to restore from. Stop and investigate manually."
            }
        }
        return [PSCustomObject]@{
            State = 'NotInstalled'; LiveDll = $liveDll; OrigDll = $origDll
            Detail = "Pristine: MediaDB64.dll looks like a real (non-forwarding) module."
        }
    }

    if ($isForwarder) {
        return [PSCustomObject]@{
            State = 'Installed'; LiveDll = $liveDll; OrigDll = $origDll
            Detail = "MediaDB64.dll forwards to MediaDB64_orig.dll  -  proxy is installed."
        }
    }
    return [PSCustomObject]@{
        State = 'UpdateDetected'; LiveDll = $liveDll; OrigDll = $origDll
        Detail = "MediaDB64.dll is a real (non-forwarding) module sitting next to a stale MediaDB64_orig.dll  -  looks like a PotPlayer update overwrote the proxy. Run Repair."
    }
}

function Install-PotPlayerProxy {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string]$ProxyPath,
        [switch]$AllowRunning
    )

    if (-not (Test-Path -LiteralPath $ProxyPath -PathType Leaf)) {
        throw "Proxy DLL not found at '$ProxyPath'. Build it first (cmake --build build --config Release)."
    }
    if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) {
        throw "Install directory '$InstallDir' does not exist."
    }
    if (-not (Test-DirectoryWritable -Path $InstallDir)) {
        throw [System.UnauthorizedAccessException]::new("No write access to '$InstallDir'. Re-run elevated.")
    }
    if (-not $AllowRunning -and (Test-PotPlayerRunning)) {
        throw "PotPlayer is running  -  a loaded DLL can't be replaced. Close it and retry."
    }

    $state = Get-ProxyInstallState -InstallDir $InstallDir
    if ($state.State -eq 'Installed') {
        throw "A proxy install is already present in '$InstallDir' (refusing to double-install). Use Uninstall first, or Repair if PotPlayer was updated."
    }
    if ($state.State -eq 'UpdateDetected') {
        throw "A stale install was detected (PotPlayer appears to have been updated over the proxy). Use the Repair action instead of Install."
    }
    if ($state.State -ne 'NotInstalled') {
        throw "Install directory is in an unexpected state ($($state.State)): $($state.Detail)"
    }

    $originalInfo = Get-PEExportInfo -Path $state.LiveDll
    $proxyInfo = Get-PEExportInfo -Path $ProxyPath
    if ($proxyInfo.Exports.Count -eq 0) {
        throw "The proxy DLL at '$ProxyPath' exports nothing  -  refusing to install what looks like the wrong file."
    }
    $match = Test-ForwarderSetMatches -ProxyExports $proxyInfo.Exports -OriginalExports $originalInfo.Exports
    if (-not $match.Matches) {
        throw "The proxy's forwarder set does not match this PotPlayer's MediaDB64.dll exports:`n$($match.MissingDetails -join "`n")`nRefusing to install  -  this PotPlayer version's MediaDB64.dll may differ from what the proxy expects."
    }

    $liveDll = $state.LiveDll
    $origDll = $state.OrigDll
    $liveLeaf = Split-Path -Leaf $liveDll
    $origLeaf = Split-Path -Leaf $origDll
    $tmpNew = "$liveDll.tmp_install"
    $tmpLeaf = Split-Path -Leaf $tmpNew

    if (Test-Path -LiteralPath $tmpNew) { Remove-Item -LiteralPath $tmpNew -Force }
    Copy-Item -LiteralPath $ProxyPath -Destination $tmpNew -Force

    # Rename the real DLL aside first, then move the already-copied proxy
    # into place last  -  the step most likely to fail (a copy, which can hit
    # disk-space/permission errors) happens *before* anything the user's
    # install currently depends on is touched.
    Rename-Item -LiteralPath $liveDll -NewName $origLeaf
    try {
        Rename-Item -LiteralPath $tmpNew -NewName $liveLeaf
    } catch {
        Rename-Item -LiteralPath $origDll -NewName $liveLeaf -ErrorAction SilentlyContinue
        throw "Failed to place the proxy DLL after renaming the original aside; rolled back. $($_.Exception.Message)"
    }

    return Get-ProxyInstallState -InstallDir $InstallDir
}

function Uninstall-PotPlayerProxy {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$InstallDir,
        [switch]$AllowRunning
    )

    if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) {
        throw "Install directory '$InstallDir' does not exist."
    }
    if (-not (Test-DirectoryWritable -Path $InstallDir)) {
        throw [System.UnauthorizedAccessException]::new("No write access to '$InstallDir'. Re-run elevated.")
    }
    if (-not $AllowRunning -and (Test-PotPlayerRunning)) {
        throw "PotPlayer is running  -  a loaded DLL can't be replaced. Close it and retry."
    }

    $state = Get-ProxyInstallState -InstallDir $InstallDir
    if ($state.State -eq 'NotInstalled') {
        return $state   # idempotent: nothing to uninstall
    }
    if ($state.State -ne 'Installed' -and $state.State -ne 'UpdateDetected') {
        throw "Install directory is in an unexpected state ($($state.State)): $($state.Detail)"
    }

    $liveDll = $state.LiveDll
    $origDll = $state.OrigDll

    if ($state.State -eq 'UpdateDetected') {
        # MediaDB64.dll is already the real, current module  -  the only stale
        # thing left is the old backup, so uninstall is just dropping it.
        Remove-Item -LiteralPath $origDll -Force
        return Get-ProxyInstallState -InstallDir $InstallDir
    }

    $liveLeaf = Split-Path -Leaf $liveDll
    $tmpAside = "$liveDll.tmp_remove"
    $tmpLeaf = Split-Path -Leaf $tmpAside
    if (Test-Path -LiteralPath $tmpAside) { Remove-Item -LiteralPath $tmpAside -Force }

    # Move the proxy aside (not delete) before restoring the original, so a
    # failure restoring still leaves every file recoverable.
    Rename-Item -LiteralPath $liveDll -NewName $tmpLeaf
    try {
        Rename-Item -LiteralPath $origDll -NewName $liveLeaf
    } catch {
        Rename-Item -LiteralPath $tmpAside -NewName $liveLeaf -ErrorAction SilentlyContinue
        throw "Failed to restore the original DLL; rolled back. $($_.Exception.Message)"
    }
    Remove-Item -LiteralPath $tmpAside -Force -ErrorAction SilentlyContinue

    return Get-ProxyInstallState -InstallDir $InstallDir
}

function Repair-PotPlayerProxy {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$InstallDir,
        [Parameter(Mandatory)][string]$ProxyPath,
        [switch]$AllowRunning
    )

    if (-not (Test-Path -LiteralPath $ProxyPath -PathType Leaf)) {
        throw "Proxy DLL not found at '$ProxyPath'. Build it first (cmake --build build --config Release)."
    }
    if (-not (Test-Path -LiteralPath $InstallDir -PathType Container)) {
        throw "Install directory '$InstallDir' does not exist."
    }
    if (-not (Test-DirectoryWritable -Path $InstallDir)) {
        throw [System.UnauthorizedAccessException]::new("No write access to '$InstallDir'. Re-run elevated.")
    }
    if (-not $AllowRunning -and (Test-PotPlayerRunning)) {
        throw "PotPlayer is running  -  a loaded DLL can't be replaced. Close it and retry."
    }

    $state = Get-ProxyInstallState -InstallDir $InstallDir
    if ($state.State -eq 'Installed') {
        return $state   # idempotent: nothing to repair
    }
    if ($state.State -eq 'NotInstalled') {
        throw "No install is present in '$InstallDir'  -  use Install, not Repair."
    }
    if ($state.State -ne 'UpdateDetected') {
        throw "Install directory is in an unexpected state ($($state.State)): $($state.Detail)"
    }

    $newOriginalInfo = Get-PEExportInfo -Path $state.LiveDll
    $proxyInfo = Get-PEExportInfo -Path $ProxyPath
    $match = Test-ForwarderSetMatches -ProxyExports $proxyInfo.Exports -OriginalExports $newOriginalInfo.Exports
    if (-not $match.Matches) {
        throw "PotPlayer's updated MediaDB64.dll no longer matches the proxy's forwarder set:`n$($match.MissingDetails -join "`n")`nRefusing to reinstall  -  the proxy needs to be rebuilt/updated for this PotPlayer version before it can be re-wrapped. The stale backup was left in place."
    }

    # Exports still line up: the stale backup is now safe to drop, which
    # brings this back to the plain NotInstalled layout Install-PotPlayerProxy
    # expects.
    Remove-Item -LiteralPath $state.OrigDll -Force

    return Install-PotPlayerProxy -InstallDir $InstallDir -ProxyPath $ProxyPath -AllowRunning:$AllowRunning
}

function Get-ProxyStatusReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$InstallDir,
        [string]$ProxyPath
    )

    $state = Get-ProxyInstallState -InstallDir $InstallDir
    $version = Get-PotPlayerVersion -InstallDir $InstallDir

    $originalMatch = 'Unknown'
    if ($ProxyPath -and (Test-Path -LiteralPath $ProxyPath -PathType Leaf)) {
        $referenceDll = if ($state.State -eq 'Installed') { $state.OrigDll } else { $state.LiveDll }
        if (Test-Path -LiteralPath $referenceDll -PathType Leaf) {
            try {
                $proxyInfo = Get-PEExportInfo -Path $ProxyPath
                $originalInfo = Get-PEExportInfo -Path $referenceDll
                $match = Test-ForwarderSetMatches -ProxyExports $proxyInfo.Exports -OriginalExports $originalInfo.Exports
                $originalMatch = if ($match.Matches) { 'Match' } else { 'Mismatch' }
            } catch {
                $originalMatch = "Error: $($_.Exception.Message)"
            }
        }
    }

    return [PSCustomObject]@{
        InstallDir       = $InstallDir
        State            = $state.State
        Detail           = $state.Detail
        PotPlayerVersion = $version
        OriginalMatch    = $originalMatch
        PotPlayerRunning = [bool](Test-PotPlayerRunning)
    }
}

Export-ModuleMember -Function `
    Get-PEExportInfo, `
    Test-ForwarderSetMatches, `
    Test-DirectoryWritable, `
    Test-PotPlayerRunning, `
    Get-PotPlayerVersion, `
    Get-ProxyInstallState, `
    Install-PotPlayerProxy, `
    Uninstall-PotPlayerProxy, `
    Repair-PotPlayerProxy, `
    Get-ProxyStatusReport
