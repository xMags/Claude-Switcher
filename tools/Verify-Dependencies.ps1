<#
.SYNOPSIS
    Proves the Release folder runs on a clean Windows PC.

.DESCRIPTION
    Every import of every binary in the output folder must resolve either
    inside that folder or to a component that ships with Windows itself.
    Anything else means the app depends on something the target machine may
    not have, which is exactly how a self-contained build stops being
    self-contained.

    "Ships with Windows" is judged by presence in System32 on this machine.
    That would also accept a redistributable someone installed there, so the
    Visual C++ runtime and the debug CRT are rejected by name: the hybrid CRT
    in build\HybridCRT.props exists so that no binary here needs them.

    Every binary must also be x64, so a stray ARM64 or x86 file cannot ship.

.PARAMETER OutputPath
    The built application folder to inspect. Defaults to the Release output.
    A Debug folder is expected to fail, because it links the debug CRT.
#>
[CmdletBinding()]
param(
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputPath) {
    $OutputPath = Join-Path $repositoryRoot 'x64\Release\ClaudeSwitcher'
}

$bannedPattern = '^(vcruntime|msvcp|concrt|vccorlib|ucrtbased|vcomp)'
$system32 = Join-Path $env:SystemRoot 'System32'

function Get-DumpBin {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer vswhere.exe was not found.'
    }
    $installationPath = & $vswhere -latest -property installationPath
    if (-not $installationPath) {
        throw 'No Visual Studio installation was found.'
    }
    $tool = Get-ChildItem -LiteralPath "$installationPath\VC\Tools\MSVC" -Directory |
        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Sort-Object -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw 'dumpbin.exe was not found in the latest Visual Studio installation.'
    }
    return $tool
}

if (-not (Test-Path -LiteralPath $OutputPath -PathType Container)) {
    throw "The application output folder was not found: $OutputPath"
}

$dumpbin = Get-DumpBin
$binaries = @(Get-ChildItem -LiteralPath $OutputPath -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
if (-not $binaries) {
    throw "No binaries were found in $OutputPath."
}

$inOutput = @{}
foreach ($binary in $binaries) {
    $inOutput[$binary.Name.ToLowerInvariant()] = $true
}

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($binary in $binaries) {
    $headers = & $dumpbin /nologo /headers $binary.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed on $($binary.FullName)."
    }
    $machine = $headers | Select-String -Pattern '^\s+[0-9A-F]+ machine \(([^)]+)\)' | Select-Object -First 1
    if (-not $machine -or $machine.Matches[0].Groups[1].Value -ne 'x64') {
        $failures.Add("$($binary.Name) is not an x64 binary.")
    }

    $dependents = & $dumpbin /nologo /dependents $binary.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed on $($binary.FullName)."
    }
    foreach ($line in $dependents) {
        if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') {
            continue
        }
        $dependency = $Matches[1].ToLowerInvariant()
        if ($dependency -match $bannedPattern) {
            $failures.Add("$($binary.Name) imports the Visual C++ runtime ($dependency).")
            continue
        }
        # API sets are resolved by the OS loader and have no file on disk.
        if ($dependency -match '^(api-ms-win-|ext-ms-win-)') {
            continue
        }
        if ($inOutput.ContainsKey($dependency) -or (Test-Path -LiteralPath (Join-Path $system32 $dependency))) {
            continue
        }
        $failures.Add("$($binary.Name) depends on $dependency, which is neither in the output folder nor part of Windows.")
    }
}

if ($failures.Count) {
    Write-Host ''
    Write-Host 'Dependency verification failed:' -ForegroundColor Red
    foreach ($failure in ($failures | Sort-Object -Unique)) {
        Write-Host "  $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Dependency closure verified across $($binaries.Count) binaries in $OutputPath." -ForegroundColor Green
