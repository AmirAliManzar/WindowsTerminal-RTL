<#
.SYNOPSIS
    Installs the portable RTL Windows Terminal for the current user.

.DESCRIPTION
    A portable build is a folder: unzip it and run WindowsTerminal.exe. This
    script does that little bit of polish that turns a folder into something
    the Start menu can find:

      - copies the build to a stable, per-user location
      - a Start menu shortcut, and a desktop one if asked
      - pins it to the taskbar is deliberately NOT done; that is the user's
        choice, and an installer that pins itself is rude

    Nothing is registered with Windows, no file associations are taken, and the
    Store copy is untouched. Uninstall is `install.ps1 -Uninstall`.

    The portable build reads the same settings as any unpackaged terminal:

        %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json

    so profiles, themes and fonts carry over with no import step. That is the
    reason this exists instead of an MSIX: a packaged install would get its own
    LocalState and would not see that settings file at all.

.PARAMETER Source
    The extracted portable build to install. Defaults to the folder this
    script is in, which is where the release zip extracts to.

.PARAMETER InstallDir
    Where to put the copy. Defaults to
    %LOCALAPPDATA%\Programs\WindowsTerminal-RTL, which needs no admin rights.

.PARAMETER NoDesktopShortcut
    Skip the desktop shortcut. The Start menu shortcut is always created.

.PARAMETER Uninstall
    Remove the install this script made.

.EXAMPLE
    .\install.ps1
    Installs from the current folder.

.EXAMPLE
    .\install.ps1 -Source C:\Downloads\WindowsTerminal-RTL-x64
#>
[CmdletBinding()]
param(
    [string]$Source = $PSScriptRoot,
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\WindowsTerminal-RTL'),
    [switch]$NoDesktopShortcut,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$AppName = 'Windows Terminal RTL'
$ExeName = 'WindowsTerminal.exe'

# Resolve the default source late. A parameter default is evaluated while the
# param block binds, and on Windows PowerShell 5.1 $PSScriptRoot is still empty
# at that point when the script was launched with -File - which is exactly what
# "Run with PowerShell" does. Falling back here means a double-click install
# works instead of dying with
#   Cannot bind argument to parameter 'Path' because it is an empty string.
if ([string]::IsNullOrEmpty($Source)) {
    $Source = if ($PSScriptRoot) { $PSScriptRoot }
               else { Split-Path -Parent $MyInvocation.MyCommand.Path }
}

if ($Uninstall) {
    Write-Host "uninstalling $AppName"
    $startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) "$AppName.lnk"
    $desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk"
    foreach ($lnk in @($startMenu, $desktop)) {
        if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force; Write-Host "  removed $lnk" }
    }
    if (Test-Path -LiteralPath $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
        Write-Host "  removed $InstallDir"
    } else {
        Write-Host "  $InstallDir not present, nothing to remove"
    }
    Write-Host "done. Your settings at %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json are untouched."
    return
}

$srcExe = Join-Path $Source $ExeName
if (-not (Test-Path -LiteralPath $srcExe)) {
    throw "$ExeName not found in '$Source'. Point -Source at an extracted portable build (the zip's contents)."
}

Write-Host "installing $AppName"
Write-Host "  from : $Source"
Write-Host "  to   : $InstallDir"

if (Test-Path -LiteralPath $InstallDir) {
    # Overwrite in place so a pinned shortcut keeps pointing at the same exe.
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

# -Path (not -LiteralPath) so the * wildcard expands; Copy-Item -Recurse with
# a trailing * is the reliable way to merge a whole tree.
Copy-Item -Path (Join-Path $Source '*') -Destination $InstallDir -Recurse -Force
$destExe = Join-Path $InstallDir $ExeName
if (-not (Test-Path -LiteralPath $destExe)) { throw "copy did not land $ExeName in $InstallDir" }
Write-Host "  copied $((Get-ChildItem -LiteralPath $InstallDir -Recurse -File).Count) files"

$shell = New-Object -ComObject WScript.Shell

function New-Shortcut([string]$Path, [string]$Description) {
    $lnk = $shell.CreateShortcut($Path)
    $lnk.TargetPath = $destExe
    $lnk.WorkingDirectory = $InstallDir
    $lnk.IconLocation = "$destExe,0"
    $lnk.Description = $Description
    $lnk.Save()
    Write-Host "  shortcut: $Path"
}

$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) "$AppName.lnk"
New-Shortcut $startMenu 'Windows Terminal with right-to-left text rendering'

if (-not $NoDesktopShortcut) {
    New-Shortcut (Join-Path ([Environment]::GetFolderPath('Desktop')) "$AppName.lnk") $AppName
}

Write-Host ""
Write-Host "done. Start menu: search for `"$AppName`"."
Write-Host "your existing settings carry over automatically:"
Write-Host "  %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json"
Write-Host "to remove: .\install.ps1 -Uninstall"
