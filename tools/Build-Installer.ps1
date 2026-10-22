<#
.SYNOPSIS
    Produces the Claude Switcher installer from a verified Release build.

.DESCRIPTION
    Runs the whole release path in order, so an installer can never come from
    an output that was not checked:

      restore packages -> build Release -> run the Core tests
      -> verify the dependency closure -> compile the installer

    The version comes from build\Version.props, the same value stamped into
    ClaudeSwitcher.exe, so the two cannot disagree.

    The installer is unsigned, so Windows SmartScreen warns about an unknown
    publisher on first run. That is expected.

.PARAMETER SkipBuild
    Reuse the existing Release output instead of rebuilding. Tests and the
    dependency check still run.
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$solution = Join-Path $repositoryRoot 'ClaudeSwitcher.slnx'
$releaseOutput = Join-Path $repositoryRoot 'x64\Release\ClaudeSwitcher'
$installerScript = Join-Path $repositoryRoot 'installer\ClaudeSwitcher.iss'
$outputDirectory = Join-Path $repositoryRoot 'installer\Output'

[xml] $versionProps = Get-Content -LiteralPath (Join-Path $repositoryRoot 'build\Version.props') -Raw
$namespace = New-Object System.Xml.XmlNamespaceManager $versionProps.NameTable
$namespace.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
$versionNode = $versionProps.SelectSingleNode('//msb:ClaudeSwitcherVersion', $namespace)
$version = if ($versionNode) { $versionNode.InnerText.Trim() } else { '' }
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "build\Version.props holds an invalid version: '$version'."
}

function Get-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer vswhere.exe was not found.'
    }
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) {
        throw 'MSBuild was not found in the latest Visual Studio installation.'
    }
    return $msbuild
}

function Get-InnoCompiler {
    # Inno Setup records its install location, which covers both the per-user
    # and the all-users installer without guessing at drive layout.
    $registryKeys = @(
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
    )
    foreach ($key in $registryKeys) {
        $location = (Get-ItemProperty -Path $key -Name 'InstallLocation' -ErrorAction SilentlyContinue).InstallLocation
        if ($location) {
            $candidate = Join-Path $location 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }
    throw 'Inno Setup 6 was not found. Install it from https://jrsoftware.org/isdl.php and run this again.'
}

if (-not $SkipBuild) {
    $msbuild = Get-MSBuild

    Write-Host 'Restoring NuGet packages...' -ForegroundColor White
    & $msbuild $solution -t:Restore -p:RestorePackagesConfig=true -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Package restore failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building Release x64 $version..." -ForegroundColor White
    & $msbuild $solution -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "The Release build failed with exit code $LASTEXITCODE."
    }
}

$executable = Join-Path $releaseOutput 'ClaudeSwitcher.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The Release output is missing: $releaseOutput"
}
# Without this index XAML cannot resolve ms-appx:/// and the app dies at
# startup, which is invisible until someone runs the installed copy.
if (-not (Test-Path -LiteralPath (Join-Path $releaseOutput 'resources.pri'))) {
    throw 'The Release output has no resources.pri, so the installed app would fail to start.'
}
$stampedVersion = (Get-Item -LiteralPath $executable).VersionInfo.ProductVersion
if ($stampedVersion -ne $version) {
    throw "ClaudeSwitcher.exe reports version '$stampedVersion' but build\Version.props says '$version'. Rebuild without -SkipBuild."
}

Write-Host 'Running the Core tests...' -ForegroundColor White
& (Join-Path $repositoryRoot 'x64\Release\CoreTests.exe')
if ($LASTEXITCODE -ne 0) {
    throw 'The Core tests failed, so no installer was produced.'
}

Write-Host 'Verifying the dependency closure...' -ForegroundColor White
& (Join-Path $PSScriptRoot 'Verify-Dependencies.ps1') -OutputPath $releaseOutput
if ($LASTEXITCODE -ne 0) {
    throw 'Dependency verification failed, so no installer was produced.'
}

$iscc = Get-InnoCompiler
Write-Host "Compiling the installer with $iscc..." -ForegroundColor White
& $iscc "/DAppVersion=$version" $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "The Inno Setup compiler failed with exit code $LASTEXITCODE."
}

$setupPath = Join-Path $outputDirectory "ClaudeSwitcher-$version-Setup.exe"
if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "The installer was not produced at $setupPath."
}

$hash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
$size = (Get-Item -LiteralPath $setupPath).Length
Write-Host ''
Write-Host 'Installer ready.' -ForegroundColor Green
Write-Host "  path:   $setupPath" -ForegroundColor White
Write-Host ("  size:   {0:N1} MB" -f ($size / 1MB)) -ForegroundColor White
Write-Host "  sha256: $hash" -ForegroundColor White
Write-Host ''
Write-Host 'The installer is unsigned, so SmartScreen will warn about an unknown publisher.' -ForegroundColor Yellow
