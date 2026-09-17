<#
.SYNOPSIS
Fast, no-PotPlayer-needed tests for PbfCodec.psm1 (PTS-018).

.DESCRIPTION
Unlike Run-IntegrationTests.ps1 (this folder's opt-in live-PotPlayer suite),
this one needs nothing but PowerShell  -  it checks the .pbf codec against
docs/FINDINGS.md section 1's real captured examples byte-for-byte, the same
"measured, not guessed" ground truth the C++ core::BuildPlaySkipSection /
core::ParsePlaySkipSection pair is tested against in
tests/unit/core/pbf_test.cpp. Run this whenever PbfCodec.psm1 changes, since
Run-IntegrationTests.ps1's own millisecond-exact assertions are only as
trustworthy as this codec is.

.EXAMPLE
powershell -File tests\integration\PbfCodec.Tests.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'PbfCodec.psm1') -Force

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

function Assert-Throws([scriptblock]$Body, [string]$Message) {
    $threw = $false
    try { & $Body } catch { $threw = $true }
    if (-not $threw) { throw "Assertion failed: $Message (expected a throw, nothing was thrown)" }
}

# ---------------------------------------------------------------------------
# docs/FINDINGS.md section 1's first real captured example: a single range,
# Start 00:12:34.567, Length 671.111s, Type = File-specific.
# ---------------------------------------------------------------------------
Test-Case "ConvertTo-PbfContent matches the single-range captured example byte-for-byte" {
    $content = ConvertTo-PbfContent -Ranges @(
        [PSCustomObject]@{ StartMs = 754567; EndMs = 754567 + 671111 }
    )
    $expected = "[PlaySkip]`r`n0=1*754567*671111`r`n1=`r`n"
    Assert-Equal $expected $content "single-range .pbf text"
}

Test-Case "ConvertFrom-PbfContent round-trips the single-range captured example" {
    $content = "[PlaySkip]`r`n0=1*754567*671111`r`n1=`r`n"
    $entries = ConvertFrom-PbfContent -Content $content
    Assert-Equal 1 $entries.Count "entry count"
    Assert-Equal 0 $entries[0].Index "index"
    Assert-Equal 1 $entries[0].Type "type"
    Assert-Equal 754567 $entries[0].StartMs "start ms"
    Assert-Equal 671111 $entries[0].LengthMs "length ms"
    Assert-Equal 1425678 $entries[0].EndMs "end ms (start + length)"
}

# ---------------------------------------------------------------------------
# docs/FINDINGS.md section 1's second captured example: 11 ranges. Checking
# the first, last, and count is enough to catch an off-by-one in either
# direction without hand-transcribing all 11 lines.
# ---------------------------------------------------------------------------
Test-Case "ConvertFrom-PbfContent parses the 11-range captured example (first/last/count)" {
    $content = "[PlaySkip]`r`n" +
        "0=1*41074*11111`r`n" +
        "1=1*477610*27027`r`n" +
        "10=1*1306906*365232`r`n" +
        "11=`r`n"
    $entries = ConvertFrom-PbfContent -Content $content
    Assert-Equal 3 $entries.Count "entry count (3 data lines in this trimmed fixture)"
    Assert-Equal 41074 $entries[0].StartMs "first entry start ms"
    Assert-Equal 11111 $entries[0].LengthMs "first entry length ms"
    Assert-Equal 1306906 $entries[2].StartMs "last entry start ms"
    Assert-Equal 365232 $entries[2].LengthMs "last entry length ms"
}

Test-Case "round trip: ConvertTo-PbfContent then ConvertFrom-PbfContent reproduces the requested ranges" {
    $requested = @(
        [PSCustomObject]@{ StartMs = 1000; EndMs = 5000 },
        [PSCustomObject]@{ StartMs = 60000; EndMs = 65500 },
        [PSCustomObject]@{ StartMs = 3723000; EndMs = 3730000 }
    )
    $content = ConvertTo-PbfContent -Ranges $requested
    $entries = ConvertFrom-PbfContent -Content $content
    Assert-Equal $requested.Count $entries.Count "round-trip entry count"
    for ($i = 0; $i -lt $requested.Count; $i++) {
        Assert-Equal $requested[$i].StartMs $entries[$i].StartMs "round-trip start ms [$i]"
        Assert-Equal $requested[$i].EndMs $entries[$i].EndMs "round-trip end ms [$i]"
        Assert-Equal $i $entries[$i].Index "round-trip index [$i]"
    }
}

Test-Case "ConvertTo-PbfContent of an empty range list is just the header + terminator" {
    $content = ConvertTo-PbfContent -Ranges @()
    Assert-Equal "[PlaySkip]`r`n0=`r`n" $content "empty .pbf text"
}

Test-Case "ConvertFrom-PbfContent of an empty (header + terminator only) section yields no entries" {
    $entries = ConvertFrom-PbfContent -Content "[PlaySkip]`r`n0=`r`n"
    Assert-Equal 0 $entries.Count "empty entry count"
}

Test-Case "ConvertFrom-PbfContent throws on a line that isn't type*start*length shaped" {
    Assert-Throws { ConvertFrom-PbfContent -Content "[PlaySkip]`r`n0=garbage`r`n1=`r`n" } `
        "malformed .pbf line should throw"
}

Test-Case "ConvertTo-PbfContent throws on a backwards/zero-length range rather than writing it" {
    Assert-Throws { ConvertTo-PbfContent -Ranges @([PSCustomObject]@{ StartMs = 5000; EndMs = 5000 }) } `
        "zero-length range should throw"
    Assert-Throws { ConvertTo-PbfContent -Ranges @([PSCustomObject]@{ StartMs = 5000; EndMs = 1000 }) } `
        "backwards range should throw"
}

# ---------------------------------------------------------------------------
# File I/O: UTF-16LE + BOM, matching docs/FINDINGS.md section 1's
# "hex/encoding inspection" measurement.
# ---------------------------------------------------------------------------
$tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("pbfcodec_tests_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmpRoot -Force | Out-Null
try {
    Test-Case "Read-PbfFile decodes a real UTF-16LE+BOM .pbf file correctly" {
        $path = Join-Path $tmpRoot 'sample.pbf'
        $text = "[PlaySkip]`r`n0=1*754567*671111`r`n1=`r`n"
        $bytes = [byte[]](0xFF, 0xFE) + [System.Text.Encoding]::Unicode.GetBytes($text)
        [System.IO.File]::WriteAllBytes($path, $bytes)

        $decoded = Read-PbfFile -Path $path
        Assert-Equal $text $decoded "decoded .pbf text"

        $entries = Get-PbfRanges -Path $path
        Assert-Equal 1 $entries.Count "Get-PbfRanges entry count"
        Assert-Equal 754567 $entries[0].StartMs "Get-PbfRanges start ms"
    }

    Test-Case "Read-PbfFile and Get-PbfRanges return `$null for a missing file (deleted .pbf, not an empty one)" {
        $path = Join-Path $tmpRoot 'does_not_exist.pbf'
        Assert-True ($null -eq (Read-PbfFile -Path $path)) "Read-PbfFile on missing file"
        Assert-True ($null -eq (Get-PbfRanges -Path $path)) "Get-PbfRanges on missing file"
    }

    Test-Case "Get-PbfPath swaps the video extension for .pbf" {
        $video = Join-Path $tmpRoot 'clip.mp4'
        Assert-Equal (Join-Path $tmpRoot 'clip.pbf') (Get-PbfPath -VideoPath $video) "derived .pbf path"
    }
} finally {
    Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "$script:passCount passed, $script:failCount failed" -ForegroundColor ($(if ($script:failCount -eq 0) { 'Green' } else { 'Red' }))
if ($script:failCount -gt 0) { exit 1 }
exit 0
