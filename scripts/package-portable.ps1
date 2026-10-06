<#
.SYNOPSIS
    Packages a built Windows Terminal into a portable, side-by-side folder.

.DESCRIPTION
    Copies files and nothing else. Nothing is installed, registered, or
    modified, and the Microsoft Store version of Windows Terminal is not
    touched in any way - the two can be run at the same time.

    The result runs straight from the folder: WindowsTerminal.exe has no
    installer, no registry entries, and no package identity, so it starts in
    portable mode and keeps its settings next to the executable rather than in
    the Store app's LocalState.

.PARAMETER BinDir
    The build output directory, e.g. <repo>\bin\x64\Release.

.PARAMETER OutDir
    Where to write the portable folder. Removed first if it already exists.

.PARAMETER Arch
    Used only for the marker file and the console title, so you can tell the
    two architectures apart in a folder listing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File dep\package-portable.ps1 `
        -BinDir bin\x64\Release -OutDir dist\WindowsTerminal-RTL-x64 -Arch x64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $BinDir,

    [Parameter(Mandatory)]
    [string] $OutDir,

    [string] $Arch = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$BinDir = (Resolve-Path -LiteralPath $BinDir -ErrorAction Stop).Path

# The build puts the exe in its own folder rather than straight into bin\, because
  # src/common.build.pre.props:11 gives C++/WinRT projects a project-specific output
  # directory. For a plain x64 Release build that is
  #
  #   bin\x64\Release\WindowsTerminal\WindowsTerminal.exe
  #
  # so search instead of assuming: the layout differs per configuration and platform,
  # and a wrong guess here only surfaces after a 30 minute build.
  $found = @(Get-ChildItem -LiteralPath $BinDir -Filter 'WindowsTerminal.exe' -File -Recurse -ErrorAction SilentlyContinue)
  if ($found.Count -eq 0) {
      throw "WindowsTerminal.exe not found anywhere under $BinDir. Build first (see README)."
  }
  if ($found.Count -gt 1) {
      # Deterministic, and say so: silently picking one would make the packaged
      # output depend on directory enumeration order.
      $names = ($found | ForEach-Object { $_.FullName }) -join ([Environment]::NewLine + '  ')
      throw "found $($found.Count) WindowsTerminal.exe under $BinDir, expected exactly one:" + [Environment]::NewLine + "  $names"
  }
  $exe = $found[0].FullName

Write-Host "[package] source : $BinDir"
Write-Host "[package] target : $OutDir"
Write-Host "[package] arch   : $Arch"

if (Test-Path -LiteralPath $OutDir) {
    Write-Host "[package] removing previous output"
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

# Everything an unpackaged Windows Terminal needs at runtime: the shell, the
# conhost it hosts, the renderer and control DLLs, the PRI and its resources,
# the settings model, and the Monaco editor assets.
$include = @(
    'WindowsTerminal.exe',
    'wt.exe',
    'OpenConsole.exe',
    'conhost.exe',
    '*.dll',
    '*.pri',
    '*.json',
    '*.ttf',
    '*.otf',
    'Monaco',
    'UnpackagedLayout'
)

# Never ship symbols. They are large, and nothing here is meant to be debugged.
$exclude = @('*.pdb', '*.ipdb')

function Test-Excluded {
    param([System.IO.FileSystemInfo] $Item)
    return $exclude -contains ('*' + $Item.Extension)
}

function Copy-Tree {
    param(
        [System.IO.DirectoryInfo] $Source,
        [string] $Destination
    )
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }
    foreach ($f in (Get-ChildItem -LiteralPath $Source.FullName -Recurse -File -Force -ErrorAction SilentlyContinue)) {
        if (Test-Excluded $f) { continue }
        $rel = $f.FullName.Substring($Source.FullName.Length).TrimStart('\')
        $to = Join-Path $Destination $rel
        $toDir = Split-Path -Path $to -Parent
        if (-not (Test-Path -LiteralPath $toDir)) {
            New-Item -ItemType Directory -Path $toDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $f.FullName -Destination $to -Force
    }
}

foreach ($pattern in $include) {
    # Directories listed in $include are copied whole. A -File filter cannot match
    # a directory, so 'Monaco' and 'UnpackagedLayout' matched nothing at all and
    # the Monaco editor assets silently never shipped: the portable build had no
    # Monaco directory, which the release archive carried on to the user.
    foreach ($dir in (Get-ChildItem -LiteralPath $BinDir -Directory -Filter $pattern -Recurse -ErrorAction SilentlyContinue)) {
        $rel = $dir.FullName.Substring($BinDir.Length).TrimStart('\')
        Write-Host "[package] dir   : $rel"
        Copy-Tree -Source $dir -Destination (Join-Path $OutDir $rel)
    }

    Get-ChildItem -LiteralPath $BinDir -Filter $pattern -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { -not (Test-Excluded $_) } |
        ForEach-Object {
            $relative = $_.FullName.Substring($BinDir.Length).TrimStart('\')
            $dest = Join-Path $OutDir $relative
            $destDir = Split-Path -Path $dest -Parent
            if (-not (Test-Path -LiteralPath $destDir)) {
                New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            }
            Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
        }
}

# The copy above preserves the build tree's directory layout, which is what the
# per-component .pri files need: Microsoft.Terminal.Control.pri exists twice under
# bin\ and flattening would silently drop one of them. But an unpackaged Terminal
# is launched by double-clicking the exe, so the executables also have to sit at
# the root of the output folder, next to the resources the shell loads relative to
# itself.
#
# This is what src/cascadia/WindowsTerminal/WindowsTerminal.vcxproj's
# _WTPrepareUnpackagedLayoutForRun target arranges for a from-the-IDE run, except
# that one is gated on '$(BuildingInsideVisualStudio)'==true and so never runs in a
# command-line build. The workflow calls that target explicitly, then this step
# mirrors it: take everything the prepare step put beside the exe, plus the
# executables, and also expose each exe at the root.
$rootExes = @('WindowsTerminal.exe', 'wt.exe', 'OpenConsole.exe', 'conhost.exe')

foreach ($name in $rootExes) {
    Get-ChildItem -LiteralPath $BinDir -Filter $name -File -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object {
            $dest = Join-Path $OutDir $name
            Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
            Write-Host "[package] root : $name  (from $($_.Directory.Name))"
        }
}

# Whatever the prepare step placed next to the exe also belongs at the root, so a
# flat launcher folder is self-contained.
#
# The symbol exclusion applies here too. It used not to, and that is where 660 MB
# of .pdb came from: this loop copies whatever it finds beside the exe, and the
# build leaves the symbols there, so the $exclude list - which only guarded the
# $include loop above - had no effect on it. The archive was 161 MB of which about
# 85% was debug symbols for binaries nobody is going to debug.
$exeDir = Split-Path -Path $exe -Parent
foreach ($item in (Get-ChildItem -LiteralPath $exeDir -Force -ErrorAction SilentlyContinue)) {
    if ($exclude -contains ('*' + $item.Extension)) { continue }
    $dest = Join-Path $OutDir $item.Name
    if (Test-Path -LiteralPath $dest) { continue }
    if ($item.PSIsContainer) {
        Copy-Item -LiteralPath $item.FullName -Destination $dest -Recurse -Force
    } else {
        Copy-Item -LiteralPath $item.FullName -Destination $dest -Force
    }
}

# Fonts. Windows Terminal's NearbyFontLoading feature (Feature_NearbyFontLoading
# in src/features.xml, AlwaysEnabled for the terminal) builds its font collection
# from every .ttf sitting next to the .exe, and gives those files precedence over
# the installed system fonts. So dropping Cascadia into the folder is enough to
# make the terminal render its own typeface on a machine that has no fonts
# installed at all - nothing is registered, no admin rights are needed, and a
# Server Core box still gets text.
#
# This is the concrete answer to "it must carry everything it needs": the fonts
# travel with the binary. The Arabic block coverage of Cascadia Code/Mono
# (217 of 256 codepoints, including every Persian-specific letter) is what lets
# Persian render before any system font is consulted. Italic is skipped on
# purpose: it carries no Arabic at all, so shipping it would add weight without
# adding a single RTL glyph.
foreach ($font in (Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '..\res\fonts') -Filter '*.ttf' -ErrorAction SilentlyContinue)) {
    if ($font.Name -like '*Italic*') { continue }
    $dest = Join-Path $OutDir $font.Name
    Copy-Item -LiteralPath $font.FullName -Destination $dest -Force
    Write-Host "[package] font : $($font.Name)"
}

# A marker so the launcher and any future patcher can tell this folder apart
# from an installed copy.
@{
    arch    = $Arch
    variant = 'WindowsTerminal-RTL'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'portable.json') -Encoding UTF8

if (-not (Test-Path -LiteralPath (Join-Path $OutDir 'WindowsTerminal.exe'))) {
    throw 'Packaging finished but WindowsTerminal.exe is missing from the output.'
}

$files = Get-ChildItem -LiteralPath $OutDir -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum

Write-Host ("[package] {0} files, {1} MB" -f $files.Count, [math]::Round($bytes / 1MB, 1))
Write-Host '[package] done. Run WindowsTerminal.exe from that folder; nothing was installed.'