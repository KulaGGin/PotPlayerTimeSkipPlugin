<#
.SYNOPSIS
PTS-007 CLI: install / uninstall / status / repair the MediaDB64 proxy in a
PotPlayer install.

.DESCRIPTION
Thin, interactive front-end over PotPlayerProxy.psm1. Handles the two things
that make this unsafe to just run blind:
  - the install directory usually needs elevation (it's under Program Files)
  - a running PotPlayer has the DLL loaded, so it can't be replaced

Everything else (refusing to double-install, verifying the proxy's exports
match this PotPlayer's real MediaDB64.dll, never destroying the original) is
enforced by the module  -  see PotPlayerProxy.psm1.

.EXAMPLE
powershell -File tools\install\potplayer-proxy.ps1 Status

.EXAMPLE
powershell -File tools\install\potplayer-proxy.ps1 Install

.EXAMPLE
powershell -File tools\install\potplayer-proxy.ps1 Repair -InstallDir "D:\Apps\PotPlayer"
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Install', 'Uninstall', 'Status', 'Repair')]
    [string]$Action = 'Status',

    [string]$InstallDir = (Join-Path $Env:ProgramFiles 'PotPlayer'),

    [string]$ProxyPath,

    # Internal: set when this script has relaunched itself elevated, so it
    # doesn't try to relaunch again if something else is still wrong.
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PotPlayerProxy.psm1') -Force

function Resolve-DefaultProxyPath {
    $repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $candidates = @(
        (Join-Path $repoRoot 'build\proxy\Release\MediaDB64.dll'),
        (Join-Path $repoRoot 'build\proxy\RelWithDebInfo\MediaDB64.dll'),
        (Join-Path $repoRoot 'build\proxy\MinSizeRel\MediaDB64.dll'),
        (Join-Path $repoRoot 'build\proxy\Debug\MediaDB64.dll')
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
    }
    return $null
}

if (-not $ProxyPath -and $Action -in @('Install', 'Repair')) {
    $ProxyPath = Resolve-DefaultProxyPath
    if (-not $ProxyPath) {
        Write-Error "Could not find a built MediaDB64.dll under build\proxy\*\. Build the project first (cmake --build build --config Release), or pass -ProxyPath explicitly."
        exit 1
    }
    Write-Host "Using proxy build at: $ProxyPath"
}

$needsWrite = $Action -in @('Install', 'Uninstall', 'Repair')

if ($needsWrite -and -not (Test-DirectoryWritable -Path $InstallDir)) {
    if ($Elevated) {
        Write-Error "Still no write access to '$InstallDir' even when elevated. Check the path is correct."
        exit 1
    }
    Write-Warning "No write access to '$InstallDir'  -  relaunching elevated."
    $relaunchArgs = @($Action, '-InstallDir', $InstallDir, '-Elevated')
    if ($ProxyPath) { $relaunchArgs += @('-ProxyPath', $ProxyPath) }
    $quoted = ($relaunchArgs | ForEach-Object { '"{0}"' -f $_ }) -join ' '
    $psArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" $quoted"
    try {
        $proc = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $psArgs -Wait -PassThru
        exit $proc.ExitCode
    } catch {
        Write-Error "Elevation was cancelled or failed: $($_.Exception.Message)"
        exit 1
    }
}

if ($needsWrite) {
    $running = Test-PotPlayerRunning
    while ($running) {
        $pids = ($running | ForEach-Object { $_.Id }) -join ', '
        Write-Warning "PotPlayer is running (PID $pids)  -  a loaded DLL can't be replaced."
        $response = Read-Host "Close PotPlayer, then press Enter to continue (or type 'abort' to cancel)"
        if ($response -eq 'abort') {
            Write-Host "Aborted."
            exit 1
        }
        $running = Test-PotPlayerRunning
    }
}

try {
    switch ($Action) {
        'Install' {
            $result = Install-PotPlayerProxy -InstallDir $InstallDir -ProxyPath $ProxyPath -AllowRunning
            Write-Host "Installed. $($result.Detail)"
        }
        'Uninstall' {
            $result = Uninstall-PotPlayerProxy -InstallDir $InstallDir -AllowRunning
            Write-Host "Uninstalled. $($result.Detail)"
        }
        'Repair' {
            $result = Repair-PotPlayerProxy -InstallDir $InstallDir -ProxyPath $ProxyPath -AllowRunning
            Write-Host "Repaired. $($result.Detail)"
        }
        'Status' {
            $report = Get-ProxyStatusReport -InstallDir $InstallDir -ProxyPath $ProxyPath
            $report | Format-List
        }
    }
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
