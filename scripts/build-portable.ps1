<#
.SYNOPSIS
    Builds a portable Windows Terminal with the RTL patch applied.

.DESCRIPTION
    Clones microsoft/terminal at UPSTREAM_REF, applies patch/windowsterminal-rtl.patch
    and patch/build-unpackaged.patch, restores the few files this repository adds
    on top of the patch, and builds it.

    Nothing here depends on a checkout of the upstream tree: the patch is the
    source of truth. That is what makes the release reproducible from this
    repository alone.

.PARAMETER Arch
    x64 or arm64.

.PARAMETER OutDir
    Where to leave the packaged build.

.PARAMETER UpstreamRef
    Build this ref instead of the one in UPSTREAM_REF.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\build-portable.ps1 -Arch x64
#>

[CmdletBinding()]
param(
    [ValidateSet('x64', 'arm64')]
    [string] $Arch = 'x64',
    [string] $OutDir = "$PWD\artifacts",
    [string] $UpstreamRef,
    [string] $WorkDir = "$env:RUNNER_TEMP\wt-build",
    [switch] $SkipPacker
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$here = Split-Path -Parent $PSScriptRoot

if (-not $UpstreamRef) {
    $UpstreamRef = (Get-Content (Join-Path $here 'UPSTREAM_REF') -Raw).Trim()
}
Write-Host "[build] upstream ref : $UpstreamRef"
Write-Host "[build] arch         : $Arch"

# ---------------------------------------------------------------- upstream

if (Test-Path -LiteralPath $WorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

$src = Join-Path $WorkDir 'terminal'
Write-Host "[build] cloning microsoft/terminal"
& git clone --filter=blob:none --no-checkout https://github.com/microsoft/terminal.git $src
& git -C $src checkout $UpstreamRef

# ---------------------------------------------------------------- patch

Write-Host "[build] applying the RTL patch"
& git -C $src apply --whitespace=nowarn (Join-Path $here 'patch\windowsterminal-rtl.patch')
if ($LASTEXITCODE -ne 0) { throw 'windowsterminal-rtl.patch did not apply cleanly' }

Write-Host "[build] applying the unpackaged build patch"
& git -C $src apply --whitespace=nowarn (Join-Path $here 'patch\build-unpackaged.patch')
if ($LASTEXITCODE -ne 0) { throw 'build-unpackaged.patch did not apply cleanly' }

# Files this repository adds on top of the patch: the solution filter, the probes
# and the packager. They are not part of the diff because they are new files, and
# a patch file cannot carry "this file did not exist upstream" for something we
# want to keep in one place.
foreach ($rel in 'WindowsTerminal.slnf') {
    Copy-Item (Join-Path $here $rel) (Join-Path $src $rel) -Force
}
foreach ($f in Get-ChildItem (Join-Path $here 'tools\bidi-probe') -Filter *.cpp) {
    New-Item -ItemType Directory -Force -Path (Join-Path $src 'tools\bidi-probe') | Out-Null
    Copy-Item $f.FullName (Join-Path $src "tools\bidi-probe\$($f.Name)") -Force
}
Copy-Item (Join-Path $here 'scripts\make-solution-filter.py') (Join-Path $src 'dep\make-solution-filter.py') -Force
Copy-Item (Join-Path $here 'scripts\package-portable.ps1') (Join-Path $src 'dep\package-portable.ps1') -Force

# The solution filter is generated from the dependency graph rather than written
# by hand, so it cannot go stale the way a hand-maintained list does.
Push-Location $src
try {
    & python dep\make-solution-filter.py | Out-Null
} finally { Pop-Location }

# ---------------------------------------------------------------- dependencies

# vcpkg.json pins builtin-baseline, and vcpkg reads versions/baseline.json out of
# that exact commit. A shallow clone does not contain it, so fetch just that one.
$vcpkg = Join-Path $WorkDir 'vcpkg'
Write-Host "[build] restoring vcpkg"
& git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
$manifest = Get-Content (Join-Path $src 'vcpkg.json') -Raw | ConvertFrom-Json
if ($manifest.'builtin-baseline') {
    & git -C $vcpkg fetch --depth 1 origin $manifest.'builtin-baseline'
}
& "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics

# These projects use packages.config rather than PackageReference, so
# `msbuild -t:restore` does not fetch them and every vcxproj fails with
# "references NuGet package(s) that are missing on this computer". nuget.exe cannot
# read the .slnx solution or the .slnf filter, so it is pointed at the
# packages.config files directly.
Write-Host "[build] restoring NuGet packages"
$nuget = (Get-Command nuget -ErrorAction SilentlyContinue).Source
if (-not $nuget) {
    $nuget = Join-Path $WorkDir 'nuget.exe'
    Invoke-WebRequest https://dist.nuget.org/win-x86-commandline/latest/nuget.exe -OutFile $nuget
}
& $nuget restore (Join-Path $src 'dep\nuget\packages.config') -NonInteractive
& $nuget restore (Join-Path $src 'build\packages.config') -NonInteractive

# ---------------------------------------------------------------- build

$platform = if ($Arch -eq 'arm64') { 'ARM64' } else { 'x64' }

$msbuildArgs = @(
    'WindowsTerminal.slnf',
    '-t:Build',
    "-p:Configuration=Release",
    "-p:Platform=$platform",
    "-p:SolutionDir=$src\",
    '-p:PgoTarget=false',
    "-p:VcpkgRoot=$vcpkg\",
    '-p:VcpkgEnabled=true',
    # The unpackaged build must keep the Store toolchain on; see
    # patch/build-unpackaged.patch for why overriding these breaks the XAML
    # compiler outright.
    '-p:LocalBuildSkipAppxSdkToolProbe=true',
    '-p:LocalBuildExplicitTargetMachine=true',
    '-p:LocalBuildDisableNewerMsvcWarnings=true',
    '-p:TreatWarningAsError=false',
    # TerminalControlLib's precompiled header is several hundred MB and gets mapped
    # once per source file under /MP, so serialise cl.exe.
    '-m:1',
    '-nr:false',
    '-v:minimal',
    '-nologo'
)

Write-Host "[build] building ($platform)"
$env:CL = '/MP1'
& msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed with exit $LASTEXITCODE" }

# ---------------------------------------------------------------- package

if (-not $SkipPacker) {
    # WindowsTerminal.vcxproj arranges the unpackaged run itself in
    # _WTPrepareUnpackagedLayoutForRun: it copies the dependency executables next
    # to the exe and merges every component's .pri into a single resources.pri.
    # That target is gated on BuildingInsideVisualStudio and so never runs in a
    # command-line build; call it explicitly rather than guessing at a file list.
    Write-Host "[build] preparing the unpackaged layout"
    & msbuild 'src\cascadia\WindowsTerminal\WindowsTerminal.vcxproj' `
        '-t:_WTPrepareUnpackagedLayoutForRun' `
        '-p:Configuration=Release' "-p:Platform=$platform" "-p:SolutionDir=$src\" `
        '-p:PgoTarget=false' "-p:VcpkgRoot=$vcpkg\" '-p:VcpkgEnabled=true' `
        '-p:LocalBuildSkipAppxSdkToolProbe=true' '-p:LocalBuildExplicitTargetMachine=true' `
        '-p:LocalBuildDisableNewerMsvcWarnings=true' '-p:TreatWarningAsError=false' `
        '-m:1' '-nr:false' '-v:minimal' '-nologo'
    if ($LASTEXITCODE -ne 0) { throw 'preparing the unpackaged layout failed' }

    $dest = Join-Path $OutDir "WindowsTerminal-RTL-$Arch"
    Write-Host "[build] packaging to $dest"
    & (Join-Path $src 'dep\package-portable.ps1') `
        -BinDir (Join-Path $src "bin\$platform\Release") `
        -OutDir $dest `
        -Arch $Arch
}

Write-Host "[build] done"
exit 0