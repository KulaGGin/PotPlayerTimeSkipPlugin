<#
.SYNOPSIS
Scripted dry-run tests for PotPlayerProxy.psm1 (PTS-007), against a
synthetic copy of the install layout  -  no real PotPlayer install needed.

.DESCRIPTION
Covers the acceptance criteria's required scenarios: fresh install,
double-install refusal, uninstall restore, post-update repair, and
exports-mismatch refusal (on both Install and Repair).

Since the real MediaDB64.dll is proprietary and never committed to this
repo (see .gitignore), "original" and "proxy" DLLs are synthesized as
minimal-but-real PE32+ files with hand-built export directories  -  just
enough structure for Get-PEExportInfo to parse, which is exactly what's
under test. This keeps the suite hermetic and network/toolchain-free.

.EXAMPLE
powershell -File tools\install\Tests.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PotPlayerProxy.psm1') -Force

# ---------------------------------------------------------------------------
# Minimal PE32+ synthesizer  -  just enough for Get-PEExportInfo to parse.
# Layout: DOS header -> PE/file/optional headers -> one section header
# (.rdata, RVA 0x200) -> at file offset 0x200, the export directory, EAT,
# names array, ordinals array, and all strings, packed contiguously so RVA
# == file offset throughout (VirtualAddress == PointerToRawData == 0x200).
# ---------------------------------------------------------------------------
function New-TestPeDll {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$DllName,
        # name -> forwarder target string ("Dll.Func"), or $null for a real export
        [Parameter(Mandatory)][hashtable]$Exports
    )

    $names = @($Exports.Keys) | Sort-Object
    $n = $names.Count

    $buf = New-Object System.Collections.Generic.List[byte]
    function Add-Bytes([byte[]]$b) { $buf.AddRange($b) }
    function Add-U16([uint16]$v) { Add-Bytes ([BitConverter]::GetBytes($v)) }
    function Add-U32([uint32]$v) { Add-Bytes ([BitConverter]::GetBytes($v)) }
    function Add-Ascii([string]$s) {
        Add-Bytes ([System.Text.Encoding]::ASCII.GetBytes($s))
        Add-Bytes ([byte[]]@(0))
    }
    function Add-FixedAscii([string]$s, [int]$len) {
        $b = [System.Text.Encoding]::ASCII.GetBytes($s)
        $out = New-Object byte[] $len
        [Array]::Copy($b, $out, [Math]::Min($b.Length, $len))
        Add-Bytes $out
    }
    function Pad-To([int]$offset) { while ($buf.Count -lt $offset) { Add-Bytes ([byte[]]@(0)) } }
    function Set-U32([int]$offset, [uint32]$v) {
        $b = [BitConverter]::GetBytes($v)
        for ($k = 0; $k -lt 4; $k++) { $buf[$offset + $k] = $b[$k] }
    }

    # DOS header: 'MZ' + e_lfanew (@0x3C) = 0x80
    Add-Bytes ([byte[]]@(0x4D, 0x5A))
    Pad-To 0x3C
    Add-U32 0x80
    Pad-To 0x80

    # PE signature + IMAGE_FILE_HEADER
    Add-Bytes ([System.Text.Encoding]::ASCII.GetBytes("PE")); Add-U16 0
    Add-U16 0x8664   # Machine = AMD64
    Add-U16 1        # NumberOfSections
    Add-U32 0; Add-U32 0; Add-U32 0   # TimeDateStamp, PointerToSymbolTable, NumberOfSymbols
    Add-U16 240      # SizeOfOptionalHeader (PE32+, 16 data directories)
    Add-U16 0x2022   # Characteristics

    $optionalHeaderStart = $buf.Count
    Add-U16 0x20B    # Magic: PE32+
    Pad-To ($optionalHeaderStart + 112)   # skip the fixed fields this parser doesn't read
    $exportDirDataDirOffset = $buf.Count
    Add-U32 0; Add-U32 0   # DataDirectory[0] (export table) placeholders
    Pad-To ($optionalHeaderStart + 240)   # remaining 15 data directories

    # One section header: .rdata, RVA 0x200, PointerToRawData 0x200
    $sectionVirtualSizeOffset = -1
    $sectionRawSizeOffset = -1
    Add-FixedAscii ".rdata" 8
    $sectionVirtualSizeOffset = $buf.Count
    Add-U32 0            # VirtualSize placeholder
    Add-U32 0x200        # VirtualAddress
    $sectionRawSizeOffset = $buf.Count
    Add-U32 0            # SizeOfRawData placeholder
    Add-U32 0x200        # PointerToRawData
    Add-U32 0; Add-U32 0 # PointerToRelocations, PointerToLinenumbers
    Add-U16 0; Add-U16 0 # NumberOfRelocations, NumberOfLinenumbers
    Add-U32 0x40000040   # Characteristics: initialized data, readable

    Pad-To 0x200

    $exportDirRva = $buf.Count
    Add-U32 0; Add-U32 0   # Characteristics, TimeDateStamp
    Add-U16 0; Add-U16 0   # Major/MinorVersion
    $nameFieldOffset = $buf.Count
    Add-U32 0               # Name RVA placeholder
    Add-U32 1               # Base
    Add-U32 $n               # NumberOfFunctions
    Add-U32 $n               # NumberOfNames
    $addrFunctionsFieldOffset = $buf.Count
    Add-U32 0
    $addrNamesFieldOffset = $buf.Count
    Add-U32 0
    $addrOrdinalsFieldOffset = $buf.Count
    Add-U32 0

    $eatRva = $buf.Count
    for ($i = 0; $i -lt $n; $i++) { Add-U32 0 }

    $namesArrayRva = $buf.Count
    for ($i = 0; $i -lt $n; $i++) { Add-U32 0 }

    $ordinalsArrayRva = $buf.Count
    for ($i = 0; $i -lt $n; $i++) { Add-U16 $i }

    $dllNameRva = $buf.Count
    Add-Ascii $DllName

    $nameStringRvas = @()
    foreach ($nm in $names) { $nameStringRvas += $buf.Count; Add-Ascii $nm }

    $forwarderStringRvas = @{}
    foreach ($nm in $names) {
        $target = $Exports[$nm]
        if ($target) { $forwarderStringRvas[$nm] = $buf.Count; Add-Ascii $target }
    }

    $exportDirEnd = $buf.Count
    $exportDirSize = $exportDirEnd - $exportDirRva

    Set-U32 $exportDirDataDirOffset $exportDirRva
    Set-U32 ($exportDirDataDirOffset + 4) $exportDirSize
    Set-U32 $nameFieldOffset $dllNameRva
    Set-U32 $addrFunctionsFieldOffset $eatRva
    Set-U32 $addrNamesFieldOffset $namesArrayRva
    Set-U32 $addrOrdinalsFieldOffset $ordinalsArrayRva

    for ($i = 0; $i -lt $n; $i++) {
        $nm = $names[$i]
        if ($forwarderStringRvas.ContainsKey($nm)) {
            Set-U32 ($eatRva + $i * 4) $forwarderStringRvas[$nm]
        } else {
            Set-U32 ($eatRva + $i * 4) 0x1000   # fake code RVA, well outside the export dir range
        }
        Set-U32 ($namesArrayRva + $i * 4) $nameStringRvas[$i]
    }

    Set-U32 $sectionVirtualSizeOffset ($exportDirEnd - 0x200)
    Set-U32 $sectionRawSizeOffset ($exportDirEnd - 0x200)

    [System.IO.File]::WriteAllBytes($Path, $buf.ToArray())
}

# ---------------------------------------------------------------------------
# Tiny test harness
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

function Assert-Throws([scriptblock]$Body, [string]$ExpectedSubstring, [string]$Message) {
    $threw = $false
    try { & $Body } catch {
        $threw = $true
        if ($ExpectedSubstring -and ($_.Exception.Message -notlike "*$ExpectedSubstring*")) {
            throw "Assertion failed: $Message (error message didn't contain '$ExpectedSubstring'; was: $($_.Exception.Message))"
        }
    }
    if (-not $threw) { throw "Assertion failed: $Message (expected a throw, nothing was thrown)" }
}

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------
$REQUIRED_EXPORTS = @('CreateDatabaseEngine', 'CreateJpegDecoder', 'CreateSMTC')

function New-OriginalDllFixture([string]$Path, [string]$DllName = 'MediaDB64.dll', [string[]]$ExportNames = $REQUIRED_EXPORTS) {
    $exports = @{}
    foreach ($e in $ExportNames) { $exports[$e] = $null }
    New-TestPeDll -Path $Path -DllName $DllName -Exports $exports
}

function New-ProxyDllFixture([string]$Path) {
    $exports = @{
        'CreateDatabaseEngine' = 'MediaDB64_orig.CreateDatabaseEngine'
        'CreateJpegDecoder'    = 'MediaDB64_orig.CreateJpegDecoder'
        'CreateSMTC'           = 'MediaDB64_orig.CreateSMTC'
    }
    New-TestPeDll -Path $Path -DllName 'MediaDB64.dll' -Exports $exports
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("pts007_tests_{0}" -f ([guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $root -Force | Out-Null

try {
    $proxyBuildPath = Join-Path $root 'proxy_build\MediaDB64.dll'
    New-Item -ItemType Directory -Path (Split-Path $proxyBuildPath) -Force | Out-Null
    New-ProxyDllFixture -Path $proxyBuildPath

    # A DLL with a real, resolvable version  -  stands in for PotPlayerMini64.exe
    # so Get-PotPlayerVersion has something genuine to read.
    $versionSourceExe = (Get-Process -Id $PID).Path

    # -----------------------------------------------------------------
    # Scenario: fresh install
    # -----------------------------------------------------------------
    $installDir = Join-Path $root 'fresh_install'
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    $originalDll = Join-Path $installDir 'MediaDB64.dll'
    New-OriginalDllFixture -Path $originalDll
    if ($versionSourceExe) { Copy-Item -LiteralPath $versionSourceExe -Destination (Join-Path $installDir 'PotPlayerMini64.exe') }
    $originalBytesBeforeInstall = [System.IO.File]::ReadAllBytes($originalDll)

    Test-Case "fresh install succeeds and produces a forwarding MediaDB64.dll" {
        $before = Get-ProxyInstallState -InstallDir $installDir
        Assert-Equal 'NotInstalled' $before.State "pre-install state"

        $result = Install-PotPlayerProxy -InstallDir $installDir -ProxyPath $proxyBuildPath -AllowRunning
        Assert-Equal 'Installed' $result.State "post-install state"
        Assert-True (Test-Path (Join-Path $installDir 'MediaDB64_orig.dll')) "backup exists after install"

        $origBytes = [System.IO.File]::ReadAllBytes((Join-Path $installDir 'MediaDB64_orig.dll'))
        Assert-True ([System.Linq.Enumerable]::SequenceEqual($origBytes, $originalBytesBeforeInstall)) "backup is byte-identical to the pre-install original"

        $liveInfo = Get-PEExportInfo -Path (Join-Path $installDir 'MediaDB64.dll')
        foreach ($e in $REQUIRED_EXPORTS) {
            Assert-True ($liveInfo.Exports.ContainsKey($e) -and $liveInfo.Exports[$e]) "live MediaDB64.dll forwards $e"
        }
    }

    Test-Case "status reports Installed, version, and export match" {
        $report = Get-ProxyStatusReport -InstallDir $installDir -ProxyPath $proxyBuildPath
        Assert-Equal 'Installed' $report.State "status State"
        Assert-True ($null -ne $report.PotPlayerVersion) "status reports a PotPlayer version"
        Assert-Equal 'Match' $report.OriginalMatch "status OriginalMatch"
    }

    # -----------------------------------------------------------------
    # Scenario: PTS-017 self-check status (Get-PluginSelfCheckStatus),
    # independent of any real MediaDB64/PotPlayer install above.
    # -----------------------------------------------------------------
    $selfCheckDir = Join-Path $root 'selfcheck'
    New-Item -ItemType Directory -Path $selfCheckDir -Force | Out-Null
    $selfCheckPath = Join-Path $selfCheckDir 'selfcheck.ini'

    Test-Case "self-check status is 'NeverRun' when the file doesn't exist yet" {
        $status = Get-PluginSelfCheckStatus -Path $selfCheckPath
        Assert-Equal 'NeverRun' $status.Result "self-check Result"
    }

    Test-Case "self-check status parses a passing run written by the plugin" {
        Set-Content -LiteralPath $selfCheckPath -Value @(
            '[SelfCheck]'
            'Result=Pass'
            'PotPlayerVersion=1.2.3.4'
            'Detail='
        ) -Encoding ascii
        $status = Get-PluginSelfCheckStatus -Path $selfCheckPath
        Assert-Equal 'Pass' $status.Result "self-check Result"
        Assert-Equal '1.2.3.4' $status.PotPlayerVersion "self-check PotPlayerVersion"
        Assert-Equal '' $status.Detail "self-check Detail"
    }

    Test-Case "self-check status parses a failing run with its detail" {
        Set-Content -LiteralPath $selfCheckPath -Value @(
            '[SelfCheck]'
            'Result=Fail'
            'PotPlayerVersion=1.2.3.4'
            'Detail=Skip Setup dialog: control id 3024 not found'
        ) -Encoding ascii
        $status = Get-PluginSelfCheckStatus -Path $selfCheckPath
        Assert-Equal 'Fail' $status.Result "self-check Result"
        Assert-Equal 'Skip Setup dialog: control id 3024 not found' $status.Detail "self-check Detail"
    }

    Test-Case "Get-ProxyStatusReport surfaces the self-check status alongside install state" {
        $report = Get-ProxyStatusReport -InstallDir $installDir -ProxyPath $proxyBuildPath -SelfCheckPath $selfCheckPath
        Assert-Equal 'Fail' $report.SelfCheckResult "report SelfCheckResult"
        Assert-Equal '1.2.3.4' $report.SelfCheckPotPlayerVersion "report SelfCheckPotPlayerVersion"
    }

    # -----------------------------------------------------------------
    # Scenario: double-install refusal
    # -----------------------------------------------------------------
    Test-Case "double-install is refused and leaves the install untouched" {
        $before = Get-ProxyInstallState -InstallDir $installDir
        Assert-Throws { Install-PotPlayerProxy -InstallDir $installDir -ProxyPath $proxyBuildPath -AllowRunning } `
            "already present" "double-install should refuse"
        $after = Get-ProxyInstallState -InstallDir $installDir
        Assert-Equal $before.State $after.State "state unchanged after refused double-install"
    }

    # -----------------------------------------------------------------
    # Scenario: uninstall restore
    # -----------------------------------------------------------------
    Test-Case "uninstall restores byte-for-byte and removes the proxy/backup" {
        $result = Uninstall-PotPlayerProxy -InstallDir $installDir -AllowRunning
        Assert-Equal 'NotInstalled' $result.State "post-uninstall state"
        Assert-True (-not (Test-Path (Join-Path $installDir 'MediaDB64_orig.dll'))) "backup removed"

        $restoredBytes = [System.IO.File]::ReadAllBytes((Join-Path $installDir 'MediaDB64.dll'))
        Assert-True ([System.Linq.Enumerable]::SequenceEqual($restoredBytes, $originalBytesBeforeInstall)) "restored MediaDB64.dll is byte-identical to the original"
    }

    Test-Case "uninstall on an already-clean install is a no-op (idempotent)" {
        $result = Uninstall-PotPlayerProxy -InstallDir $installDir -AllowRunning
        Assert-Equal 'NotInstalled' $result.State "idempotent uninstall state"
    }

    # -----------------------------------------------------------------
    # Scenario: post-update repair
    # -----------------------------------------------------------------
    $repairDir = Join-Path $root 'repair_flow'
    New-Item -ItemType Directory -Path $repairDir -Force | Out-Null
    New-OriginalDllFixture -Path (Join-Path $repairDir 'MediaDB64.dll')
    Install-PotPlayerProxy -InstallDir $repairDir -ProxyPath $proxyBuildPath -AllowRunning | Out-Null

    Test-Case "a PotPlayer update over the proxy is detected as UpdateDetected" {
        # Simulate the update: PotPlayer's installer overwrote MediaDB64.dll
        # (currently our proxy) with a new real one, leaving our stale backup.
        $newOriginal = Join-Path $repairDir 'MediaDB64.dll'
        Remove-Item -LiteralPath $newOriginal -Force
        New-OriginalDllFixture -Path $newOriginal -DllName 'MediaDB64.dll (updated)'

        $state = Get-ProxyInstallState -InstallDir $repairDir
        Assert-Equal 'UpdateDetected' $state.State "state after simulated update"
    }

    Test-Case "repair re-wraps the new original" {
        $newOriginalBytes = [System.IO.File]::ReadAllBytes((Join-Path $repairDir 'MediaDB64.dll'))

        $result = Repair-PotPlayerProxy -InstallDir $repairDir -ProxyPath $proxyBuildPath -AllowRunning
        Assert-Equal 'Installed' $result.State "post-repair state"

        $backupBytes = [System.IO.File]::ReadAllBytes((Join-Path $repairDir 'MediaDB64_orig.dll'))
        Assert-True ([System.Linq.Enumerable]::SequenceEqual($backupBytes, $newOriginalBytes)) "backup after repair is the NEW original, not the stale one"

        $liveInfo = Get-PEExportInfo -Path (Join-Path $repairDir 'MediaDB64.dll')
        foreach ($e in $REQUIRED_EXPORTS) {
            Assert-True ($liveInfo.Exports.ContainsKey($e) -and $liveInfo.Exports[$e]) "re-wrapped MediaDB64.dll forwards $e"
        }
    }

    Test-Case "repair on an already-installed proxy is a no-op (idempotent)" {
        $result = Repair-PotPlayerProxy -InstallDir $repairDir -ProxyPath $proxyBuildPath -AllowRunning
        Assert-Equal 'Installed' $result.State "idempotent repair state"
    }

    # -----------------------------------------------------------------
    # Scenario: exports-mismatch refusal (Install)
    # -----------------------------------------------------------------
    $mismatchDir = Join-Path $root 'mismatch_install'
    New-Item -ItemType Directory -Path $mismatchDir -Force | Out-Null
    # Missing CreateSMTC  -  as if this PotPlayer version's MediaDB64.dll no
    # longer has one of the three exports the proxy forwards.
    New-OriginalDllFixture -Path (Join-Path $mismatchDir 'MediaDB64.dll') -ExportNames @('CreateDatabaseEngine', 'CreateJpegDecoder')

    Test-Case "install refuses when the original's exports don't match the proxy's forwarder set" {
        Assert-Throws { Install-PotPlayerProxy -InstallDir $mismatchDir -ProxyPath $proxyBuildPath -AllowRunning } `
            "CreateSMTC" "exports-mismatch install should name the missing export"
        $state = Get-ProxyInstallState -InstallDir $mismatchDir
        Assert-Equal 'NotInstalled' $state.State "mismatch dir untouched after refusal"
        Assert-True (-not (Test-Path (Join-Path $mismatchDir 'MediaDB64_orig.dll'))) "no backup created on refused install"
    }

    # -----------------------------------------------------------------
    # Scenario: exports-mismatch refusal (Repair)  -  the stale backup must
    # survive the refusal, since deleting it before confirming the new
    # original is compatible would destroy the only way back.
    # -----------------------------------------------------------------
    $mismatchRepairDir = Join-Path $root 'mismatch_repair'
    New-Item -ItemType Directory -Path $mismatchRepairDir -Force | Out-Null
    New-OriginalDllFixture -Path (Join-Path $mismatchRepairDir 'MediaDB64.dll')
    Install-PotPlayerProxy -InstallDir $mismatchRepairDir -ProxyPath $proxyBuildPath -AllowRunning | Out-Null

    Test-Case "repair refuses when the updated original's exports changed, and keeps the backup" {
        $liveDll = Join-Path $mismatchRepairDir 'MediaDB64.dll'
        Remove-Item -LiteralPath $liveDll -Force
        New-OriginalDllFixture -Path $liveDll -ExportNames @('CreateDatabaseEngine', 'CreateJpegDecoder')

        $state = Get-ProxyInstallState -InstallDir $mismatchRepairDir
        Assert-Equal 'UpdateDetected' $state.State "state before refused repair"

        Assert-Throws { Repair-PotPlayerProxy -InstallDir $mismatchRepairDir -ProxyPath $proxyBuildPath -AllowRunning } `
            "CreateSMTC" "exports-mismatch repair should name the missing export"

        $after = Get-ProxyInstallState -InstallDir $mismatchRepairDir
        Assert-Equal 'UpdateDetected' $after.State "state unchanged after refused repair"
        Assert-True (Test-Path (Join-Path $mismatchRepairDir 'MediaDB64_orig.dll')) "stale backup preserved after refused repair"
    }

    # -----------------------------------------------------------------
    # Misc: writability probe and clear errors for a bad -InstallDir
    # -----------------------------------------------------------------
    Test-Case "Test-DirectoryWritable is true for a writable temp dir and false for a nonexistent path" {
        Assert-True (Test-DirectoryWritable -Path $root) "temp root should be writable"
        Assert-True (-not (Test-DirectoryWritable -Path (Join-Path $root 'does_not_exist'))) "nonexistent dir should not be writable"
    }

    Test-Case "a nonexistent -InstallDir fails with a clear message, not a misleading elevation prompt" {
        $bogus = Join-Path $root 'no_such_directory'
        Assert-Throws { Install-PotPlayerProxy -InstallDir $bogus -ProxyPath $proxyBuildPath -AllowRunning } `
            "does not exist" "install against a missing directory"
    }
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "$script:passCount passed, $script:failCount failed" -ForegroundColor ($(if ($script:failCount -eq 0) { 'Green' } else { 'Red' }))
if ($script:failCount -gt 0) { exit 1 }
exit 0
