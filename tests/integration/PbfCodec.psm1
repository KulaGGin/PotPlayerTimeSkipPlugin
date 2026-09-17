# PbfCodec.psm1  -  PTS-018's pure `.pbf` codec.
#
# A PowerShell port of core::BuildPlaySkipSection / core::ParsePlaySkipSection
# (core/src/core.cpp), kept deliberately separate from the live-PotPlayer
# driving modules in this folder so it can be unit-tested with no PotPlayer
# process anywhere in the loop  -  see PbfCodec.Tests.ps1. This is what lets
# Run-IntegrationTests.ps1 assert "the .pbf matches millisecond-exact"
# against the *documented* format (docs/FINDINGS.md section 1), not against
# whatever this script happens to produce.
#
# Format (measured, docs/FINDINGS.md section 1):
#   - UTF-16LE with a BOM (FF FE), CRLF line endings.
#   - "[PlaySkip]\r\n" header, then one "<index>=<type>*<start_ms>*<length_ms>\r\n"
#     line per range (0-based index), then a "<n>=\r\n" terminator line.
#   - Field 3 is a *length*, not an end time.

Set-StrictMode -Version Latest

function Get-PbfPath {
    <#
    .SYNOPSIS
    The `.pbf` sidecar path PotPlayer uses for a given video file
    (docs/FINDINGS.md section 1: "<video>.mp4 -> <video>.pbf").
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$VideoPath)

    $dir = Split-Path -Path $VideoPath -Parent
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($VideoPath)
    return Join-Path $dir "$stem.pbf"
}

function ConvertTo-PbfContent {
    <#
    .SYNOPSIS
    Builds a `[PlaySkip]` section's text content from a list of ranges  -
    the PowerShell twin of core::BuildPlaySkipSection.

    .PARAMETER Ranges
    Ordered list of objects/hashtables with StartMs/EndMs (int64 ms each).
    Index in the output is positional (0-based), matching PotPlayer's own
    behavior  -  the caller never supplies an index.

    .PARAMETER Type
    The .pbf Type field for every line; defaults to 1 (File-specific,
    core::kFileSpecificType)  -  the only value this plugin ever writes.

    .OUTPUTS
    The section text, CRLF-terminated, WITHOUT a BOM  -  that belongs to the
    file-encoding layer (Write-PbfFile-equivalent), not this pure transform.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][array]$Ranges,
        [int]$Type = 1
    )

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("[PlaySkip]`r`n")
    $index = 0
    foreach ($r in $Ranges) {
        $startMs = [int64]$r.StartMs
        $endMs = [int64]$r.EndMs
        if ($endMs -le $startMs) {
            throw "ConvertTo-PbfContent: range $index has end ($endMs) <= start ($startMs)"
        }
        $lengthMs = $endMs - $startMs
        [void]$sb.Append("$index=$Type*$startMs*$lengthMs`r`n")
        $index++
    }
    [void]$sb.Append("$index=`r`n")
    return $sb.ToString()
}

function ConvertFrom-PbfContent {
    <#
    .SYNOPSIS
    Parses a `[PlaySkip]` section's text content into its entries  -  the
    PowerShell twin of core::ParsePlaySkipSection, but keeping Index/Type
    too (core's own version discards them; PTS-018's diagnostics want them
    for mismatch dumps).

    .DESCRIPTION
    Skips a leading "[PlaySkip]" header line if present, tolerates the empty
    "N=" terminator line (silently skipped, not an error), and throws on any
    other malformed line  -  same tolerance contract as core::ParsePlaySkipSection.

    .OUTPUTS
    Array of PSCustomObject { Index, Type, StartMs, LengthMs, EndMs }, in
    file order.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string]$Content)

    $entries = New-Object System.Collections.Generic.List[object]
    $lines = $Content -split "`n"
    foreach ($rawLine in $lines) {
        $line = $rawLine
        if ($line.Length -gt 0 -and $line[-1] -eq "`r") {
            $line = $line.Substring(0, $line.Length - 1)
        }
        if ($line.Length -eq 0 -or $line -eq '[PlaySkip]') {
            continue
        }

        $eq = $line.IndexOf('=')
        if ($eq -lt 0) {
            throw "ConvertFrom-PbfContent: invalid .pbf line (missing '='): '$line'"
        }
        $indexText = $line.Substring(0, $eq)
        $rest = $line.Substring($eq + 1)
        if ($rest.Length -eq 0) {
            # Terminator line ("N="): expected exactly once, at the end.
            continue
        }

        $parts = $rest.Split('*')
        if ($parts.Length -ne 3) {
            throw "ConvertFrom-PbfContent: invalid .pbf line (expected type*start_ms*length_ms): '$line'"
        }

        $indexVal = [int]$indexText
        $typeVal = [int]$parts[0]
        $startMs = [int64]$parts[1]
        $lengthMs = [int64]$parts[2]

        $entries.Add([PSCustomObject]@{
            Index    = $indexVal
            Type     = $typeVal
            StartMs  = $startMs
            LengthMs = $lengthMs
            EndMs    = $startMs + $lengthMs
        })
    }

    # The leading comma forces this to stay an array through PowerShell's
    # pipeline-unwrapping when the caller captures it (`$x = Fn`)  -  without
    # it, a single-entry result silently collapses to a bare PSCustomObject
    # and every `.Count`/`$x[0]` call downstream misbehaves.
    return ,$entries.ToArray()
}

function Read-PbfFile {
    <#
    .SYNOPSIS
    Reads a `.pbf` file as text, decoding the UTF-16LE+BOM encoding
    docs/FINDINGS.md section 1 measured.

    .OUTPUTS
    The decoded text (BOM stripped), or $null if the file doesn't exist  -
    "no .pbf" is itself meaningful (PotPlayer deletes the file entirely once
    its last range is removed), never coerced to an empty string.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        return [System.Text.Encoding]::Unicode.GetString($bytes, 2, $bytes.Length - 2)
    }
    # Tolerate a BOM-less file (e.g. a hand-crafted test fixture) rather than
    # refusing to read it  -  real PotPlayer output always carries the BOM.
    return [System.Text.Encoding]::Unicode.GetString($bytes)
}

function Get-PbfRanges {
    <#
    .SYNOPSIS
    Reads and parses a `.pbf` file in one step: Read-PbfFile + ConvertFrom-PbfContent.

    .OUTPUTS
    Array of entries (see ConvertFrom-PbfContent), or $null if the file
    doesn't exist.
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)

    $content = Read-PbfFile -Path $Path
    if ($null -eq $content) {
        return $null
    }
    return ,(ConvertFrom-PbfContent -Content $content)
}

Export-ModuleMember -Function `
    Get-PbfPath, `
    ConvertTo-PbfContent, `
    ConvertFrom-PbfContent, `
    Read-PbfFile, `
    Get-PbfRanges
