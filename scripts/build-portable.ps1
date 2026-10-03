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

# Set before checkout, deliberately. Upstream commits a mix of line endings -
# the vcpkg overlay triplets are LF while the .cpp and .props files are CRLF - and
# the patch carries each file's context with the endings upstream actually used.
# Rewrite any of them on checkout and the patch stops matching, which is exactly
# how this build failed once already. Upstream's .gitattributes says "* -text" so
# nothing should be converted anyway, but core.autocrlf is the setting that could
# do it silently, and it is whatever the runner image happens to default to.
& git -C $src config core.autocrlf false
& git -C $src config core.eol lf
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
# The overlay triplets. These exist on main but not at v1.24.11911.0, so the
# patch cannot add them - it can only modify what is already there. They have to
# arrive as files, and they have to land before vcpkg runs, because the triplet
# is what decides which toolset the dependencies are built against.
foreach ($f in Get-ChildItem (Join-Path $here 'dep\vcpkg-overlay-triplets') -Filter *.cmake -ErrorAction SilentlyContinue) {
    Copy-Item $f.FullName (Join-Path $src "dep\vcpkg-overlay-triplets\$($f.Name)") -Force
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

# ---------------------------------------------------------------- vcpkg

# A full clone, and not a cheaper one. Both alternatives were tried and both fail:
#
#   --depth 1          read-tree <sha> exits 128, "failed to unpack tree object"
#   --filter=blob:none read-tree is satisfied but checkout-index exits 128
#
# vcpkg's msbuild integration pulls port implementations out of its own history
# with `git --git-dir <vcpkg> read-tree <sha>`, so the clone has to be able to
# serve any historical tree. It also extracts files with checkout-index, so it
# needs blobs. A full clone is the only clone that does both.
#
# The earlier successful CI run used --depth 1 and got away with it, which is not
# evidence that it works: it simply had not reached a port that needed a tree it
# did not have. That recipe then failed once it did.
$vcpkg = Join-Path $WorkDir 'vcpkg'
Write-Host "[build] cloning vcpkg (full history)"
& git clone https://github.com/microsoft/vcpkg.git $vcpkg
if ($LASTEXITCODE -ne 0) { throw "cloning vcpkg failed" }
& "$vcpkg\bootstrap-vcpkg.bat" -disableMetrics

# Install the dependencies up front, as its own step. Left to msbuild, the same
# work happens mid-build where a failed dependency restore is indistinguishable
# from a failed compile.
# From inside the clone, so the manifest and the overlay triplets are found.
#
# --overlay-triplets is the same flag pre.props passes to msbuild: without it
# vcpkg resolves <arch>-windows-static to its own built-in triplet and builds the
# dependencies with whatever toolset is newest on the runner, which is newer than
# the one this ref pins. Installing them up front with the same overlay keeps the
# two paths agreeing, and the up-front install is the one that reports a real
# error instead of a compile failure later.
$triplet = "$Arch-windows-static"
$overlay = Join-Path $src 'dep\vcpkg-overlay-triplets'
Write-Host "[build] vcpkg install ($triplet)"
Push-Location $src
try {
    & "$vcpkg\vcpkg.exe" install --triplet $triplet --overlay-triplets=$overlay
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed with exit $LASTEXITCODE" }
}
finally { Pop-Location }

# ---------------------------------------------------------------- NuGet

# These projects use packages.config rather than PackageReference, so
# `msbuild -t:restore` does not fetch them and every vcxproj fails with
# "references NuGet package(s) that are missing on this computer". nuget.exe
# cannot read the .slnx solution or the .slnf filter, so it is pointed at the
# packages.config files directly.
#
# Run from inside the clone. The root NuGet.Config sets globalPackagesFolder and
# repositorypath to .\packages, and nuget resolves those relative paths against
# the working directory - so invoked from anywhere else the packages land in the
# default global folder and every project reports them missing.
Write-Host "[build] restoring NuGet packages"
$nuget = (Get-Command nuget -ErrorAction SilentlyContinue).Source
if (-not $nuget) {
    $nuget = Join-Path $WorkDir 'nuget.exe'
    Invoke-WebRequest https://dist.nuget.org/win-x86-commandline/latest/nuget.exe -OutFile $nuget
}

# Named rather than inferred, on top of running from inside the clone: two
# independent ways for the destination to be what the projects expect.
$packages = Join-Path $src 'packages'
Push-Location $src
try {
    foreach ($cfg in 'dep\nuget\packages.config', 'build\packages.config', '.nuget\packages.config') {
        if (-not (Test-Path -LiteralPath $cfg)) { continue }
        Write-Host "  $cfg"
        & $nuget restore $cfg -NonInteractive -PackagesDirectory $packages
        if ($LASTEXITCODE -ne 0) { throw "nuget restore failed for $cfg" }
    }
}
finally { Pop-Location }

# ---------------------------------------------------------------- build

$platform = if ($Arch -eq 'arm64') { 'ARM64' } else { 'x64' }
# Every path from here on is resolved by msbuild against the process working
# directory, so the working directory is part of the build rather than incidental
# to it. Getting it wrong does not say so: it surfaces as MSB5026, a complaint
# about a solution file, which points at the wrong thing entirely.
#
# The solution file itself is whichever one this upstream ref has: newer
# microsoft/terminal ships OpenConsole.slnx, the v1.24 release tag ships the
# classic OpenConsole.sln. The filter names the one it was generated from, so
# either is fine here.
$solution = @('OpenConsole.slnx', 'OpenConsole.sln') |
    ForEach-Object { Join-Path $src $_ } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $solution) {
    throw "neither OpenConsole.slnx nor OpenConsole.sln found under $src"
}
$expect = @(
    (Join-Path $src 'WindowsTerminal.slnf'),
    $solution,
    (Join-Path $src 'src\cascadia\WindowsTerminal\WindowsTerminal.vcxproj'),
    (Join-Path $src 'dep\package-portable.ps1')
)
$absent = @($expect | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($absent) {
    foreach ($a in $absent) { Write-Host "  missing: $a" }
    throw "the upstream tree at $src is incomplete - $($absent.Count) file(s) missing"
}
Write-Host "[build] work dir    : $src"
Write-Host ""

$msbuildArgs = @(
    # Relative on purpose: the block runs from inside the clone.
    'WindowsTerminal.slnf',
    '-t:Build',
    '-p:Configuration=Release',
    "-p:Platform=$platform",
    "-p:SolutionDir=$src\",
    '-p:PgoTarget=false',
    "-p:VcpkgRoot=$vcpkg\",
    '-p:VcpkgEnabled=true',
    # The unpackaged build must keep the Store toolchain on; see
    # patch/build-unpackaged.patch for why overriding those two properties
    # disables the XAML compiler outright.
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

Push-Location $src
try {
    & msbuild @msbuildArgs
    if ($LASTEXITCODE -ne 0) { throw "build failed with exit $LASTEXITCODE" }
}
finally { Pop-Location }

# ---------------------------------------------------------------- package

if (-not $SkipPacker) {
    # WindowsTerminal.vcxproj arranges the unpackaged run itself in
    # _WTPrepareUnpackagedLayoutForRun: it copies the dependency executables next
    # to the exe and merges every component's .pri into a single resources.pri.
    # That target is gated on BuildingInsideVisualStudio and so never runs in a
    # command-line build; call it explicitly rather than guessing at a file list.
    Write-Host "[build] preparing the unpackaged layout"

    Push-Location $src
    try {
        & msbuild 'src\cascadia\WindowsTerminal\WindowsTerminal.vcxproj' `
            '-t:_WTPrepareUnpackagedLayoutForRun' `
            '-p:Configuration=Release' `
            "-p:Platform=$platform" `
            "-p:SolutionDir=$src\" `
            '-p:PgoTarget=false' `
            "-p:VcpkgRoot=$vcpkg\" `
            '-p:VcpkgEnabled=true' `
            '-p:LocalBuildSkipAppxSdkToolProbe=true' `
            '-p:LocalBuildExplicitTargetMachine=true' `
            '-p:LocalBuildDisableNewerMsvcWarnings=true' `
            '-p:TreatWarningAsError=false' `
            '-m:1' '-nr:false' '-v:minimal' '-nologo'
        if ($LASTEXITCODE -ne 0) { throw 'preparing the unpackaged layout failed' }
    }
    finally { Pop-Location }

    $dest = Join-Path $OutDir "WindowsTerminal-RTL-$Arch"
    Write-Host "[build] packaging to $dest"
    & (Join-Path $src 'dep\package-portable.ps1') `
        -BinDir (Join-Path $src "bin\$platform\Release") `
        -OutDir $dest `
        -Arch $Arch
}

Write-Host "[build] done"
exit 0
