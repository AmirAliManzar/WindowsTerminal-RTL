<#
.SYNOPSIS
    Patches the Windows Terminal installed from the Microsoft Store so Persian,
    Arabic and Hebrew render right to left.

.DESCRIPTION
    Replaces the DLLs that carry the text renderer with the ones from this
    project's portable build. The renderer is a static library, ConRenderAtlas,
    linked into TerminalApp.dll and Microsoft.Terminal.Control.dll, so those are
    the files that matter.

    Every replaced file is copied to a backup folder first, and -Revert puts them
    back. Nothing else is touched: your settings, themes and profiles stay in
    %LOCALAPPDATA%\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState.

.PARAMETER Revert
    Restore the originals from the backup folder and stop.

.PARAMETER LocalDir
    Take the replacement DLLs from this folder instead of downloading them.

.PARAMETER PackageDir
    Patch this install folder instead of the one Windows reports.

.PARAMETER NoRestart
    Do not relaunch the terminal when finished.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\patch-wt.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\patch-wt.ps1 -Revert
#>

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [switch] $Revert,
    [string] $LocalDir,
    [string] $PackageDir,
    [string] $PackageName = 'Microsoft.WindowsTerminal',
    [switch] $NoRestart
)

$ErrorActionPreference = 'Stop'

# The renderer is compiled into these. OpenConsole.exe hosts the console itself
# and has its own copy, but the window the user types into is drawn by TerminalApp.
# Verified by diffing the two install folders: these names exist in both the Store
# install and a portable build. WindowsTerminalShellExt.dll is store-only and
# console.dll / Microsoft.UI.Xaml.dll are portable-only, so none of the three are
# candidates.
$Targets = @(
    'TerminalApp.dll',
    'Microsoft.Terminal.Control.dll',
    'Microsoft.Terminal.UI.dll',
    'TerminalConnection.dll',
    'Microsoft.Terminal.Settings.Model.dll',
    'Microsoft.Terminal.Settings.Editor.dll',
    'Microsoft.Terminal.UI.Markdown.dll',
    'OpenConsoleProxy.dll',
    'TerminalThemeHelpers.dll'
)

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal $id).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Unlock-Target {
    param([string] $Path)

    # Program Files\WindowsApps is owned by TrustedInstaller, and Administrator
    # does not imply the right to write there. Being an administrator is only
    # half the permission; the file also has to be ours to write. Without these
    # two calls the copy below fails with "access denied" even from an elevated
    # prompt, which looks like a bug in this script rather than a permissions
    # fact about the Store.
    & takeown.exe /F $Path /A | Out-Null
    & icacls.exe $Path /grant '*S-1-5-32-544:F' | Out-Null
}

function Write-File {
    param([string] $Source, [string] $Destination)

    Unlock-Target -Path $Destination
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Get-InstallPath {
    if ($PackageDir) { return (Resolve-Path -LiteralPath $PackageDir).Path }
    $pkg = Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue |
           Sort-Object Version -Descending | Select-Object -First 1
    if (-not $pkg) {
        throw "$PackageName is not installed, and no -PackageDir was given."
    }
    Write-Host "found $($pkg.Name) $($pkg.Version)"
    return $pkg.InstallLocation
}

function Get-BackupDir {
    param([string] $InstallPath)
    $leaf = Split-Path -Path $InstallPath -Leaf
    return (Join-Path $env:ProgramData "WindowsTerminal-RTL\backup\$leaf")
}

function Stop-Terminal {
    Get-Process WindowsTerminal, OpenConsole -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($InstallPath, [StringComparison]::OrdinalIgnoreCase) } |
        ForEach-Object {
            Write-Host "  closing $($_.ProcessName) (pid $($_.Id))"
            try { $_.CloseMainWindow() | Out-Null } catch { }
        }
    Start-Sleep -Milliseconds 800
    Get-Process WindowsTerminal, OpenConsole -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($InstallPath, [StringComparison]::OrdinalIgnoreCase) } |
        ForEach-Object {
            Write-Host "  forcing $($_.ProcessName) (pid $($_.Id))"
            try { Stop-Process -Id $_.Id -Force } catch { }
        }
    Start-Sleep -Milliseconds 500
}

function Start-Terminal {
    $exe = Join-Path $InstallPath 'WindowsTerminal.exe'
    if (Test-Path -LiteralPath $exe) {
        Write-Host "  launching Windows Terminal"
        Start-Process -FilePath 'shell:AppsFolder\Microsoft.WindowsTerminal_8wekyb3d8bbwe!Microsoft.WindowsTerminal' -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------- revert

if ($Revert) {
    $install = Get-InstallPath
    $backup = Get-BackupDir $install
    if (-not (Test-Path -LiteralPath $backup)) {
        throw "no backup found at $backup - nothing to revert"
    }
    Stop-Terminal
    Write-Host "restoring from $backup"
    Get-ChildItem -LiteralPath $backup -Filter *.dll | ForEach-Object {
        Write-File -Source $_.FullName -Destination (Join-Path $install $_.Name)
        Write-Host "  $($_.Name)"
    }
    if (-not $NoRestart) { Start-Terminal }
    Write-Host "done."
    exit 0
}

# ---------------------------------------------------------------- patch

# Elevation is checked further down, once there is something to write. Planning
# the replacement needs no privileges, so -WhatIf has to work without them.

$install = Get-InstallPath
$backup = Get-BackupDir $install

# Where do the replacement DLLs come from?
$source = $LocalDir
if (-not $source) {
    throw "no -LocalDir given. Download the release for your architecture, unpack the
    portable build, and pass that folder, for example:
      .\WindowsTerminal-RTL-patcher.exe -LocalDir C:\path\to\WindowsTerminal-RTL-x64"
}
$source = (Resolve-Path -LiteralPath $source).Path
Write-Host "source DLLs : $source"
Write-Host "install     : $install"

$missing = @()
$planned = @()
foreach ($name in $Targets) {
    $src = Join-Path $source $name
    $dst = Join-Path $install $name
    if (-not (Test-Path -LiteralPath $src)) { $missing += $name; continue }
    if (-not (Test-Path -LiteralPath $dst)) { continue }
    $planned += @{ name = $name; src = $src; dst = $dst }
}

if ($planned.Count -eq 0) {
    throw "none of the target DLLs were found in $source. Is this the right folder?"
}
if ($missing) {
    Write-Warning "not in the source folder, skipped: $($missing -join ', ')"
}

# Show exactly what is about to change, with both sizes, so a substitution that
# turns out to be wrong is visible before anything is written.
Write-Host "file plan:"
foreach ($f in $planned) {
    $s = (Get-Item -LiteralPath $f.src).Length
    $d = (Get-Item -LiteralPath $f.dst).Length
    $flag = if ($s -eq $d) { 'same size' } else { 'DIFFERENT SIZE' }
    Write-Host ("  {0,-42} {1,10:N0} -> {2,10:N0}  {3}" -f $f.name, $d, $s, $flag)
}
Write-Host ""

if (-not $PSCmdlet.ShouldProcess($install, 'Replace the listed files')) {
    Write-Host 'Nothing was changed. Re-run without -WhatIf to apply.'
    exit 0
}

# Only now, with the plan settled and the user committed to it, does anything
# get written - and writing into WindowsApps needs the file to be ours as well
# as the process being elevated.
if (-not (Test-Admin)) {
    throw "this needs administrator rights. Right-click the patcher exe, or run:
    powershell -ExecutionPolicy Bypass -File .\patch-wt.ps1  (from an admin prompt)"
}

New-Item -ItemType Directory -Force -Path $backup | Out-Null

# Back up once, and only the first time, so a second run cannot overwrite the
# original with an already patched file.
foreach ($f in $planned) {
    $bak = Join-Path $backup $f.name
    if (-not (Test-Path -LiteralPath $bak)) {
        Copy-Item -LiteralPath $f.dst -Destination $bak -Force
        Write-Host "  backed up $($f.name)"
    }
}
Write-Host "backup      : $backup"
Write-Host ""

Stop-Terminal

$applied = @()
try {
    foreach ($f in $planned) {
        Write-File -Source $f.src -Destination $f.dst
        Write-Host "  patched $($f.name)"
        $applied += $f
    }
}
catch {
    Write-Warning "copy failed: $($_.Exception.Message)"
    Write-Host "rolling back the $($applied.Count) file(s) already written"
    foreach ($f in $applied) {
        try {
            Write-File -Source (Join-Path $backup $f.name) -Destination $f.dst
            Write-Host "  restored $($f.name)"
        }
        catch {
            Write-Warning "could not restore $($f.name): $($_.Exception.Message)"
        }
    }
    if ($applied.Count -gt 0) {
        Write-Host ""
        Write-Host "The install is in a mixed state. Finish with:"
        Write-Host "  .\WindowsTerminal-RTL-patcher.exe -Revert"
    }
    throw
}

# A record of what this install now consists of, so it is answerable later
# without re-deriving it: which files were replaced, and what the original
# bytes were.
@{
    install        = $install
    packageVersion = "$(try { (Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue | Sort-Object Version -Descending | Select-Object -First 1).Version } catch { $null })"
    patchedUtc     = (Get-Date).ToUniversalTime().ToString('s')
    sourceDir      = $source
    backupDir      = $backup
    files          = @($applied | ForEach-Object {
        @{ name = $_.name; original = (Get-Item -LiteralPath (Join-Path $backup $_.name)).Length; patched = (Get-Item -LiteralPath $_.src).Length }
    })
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding UTF8

Write-Host ""
Write-Host "patched $($applied.Count) file(s)."
Write-Host "A Store update replaces these files and removes the patch - run the patcher again after updating."
Write-Host "To undo: .\WindowsTerminal-RTL-patcher.exe -Revert"

if (-not $NoRestart) { Start-Terminal }
exit 0