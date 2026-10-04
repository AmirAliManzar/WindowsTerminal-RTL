<#
.SYNOPSIS
    Small one-file installer for Windows Terminal RTL.

.DESCRIPTION
    This is the lightest thing on the Releases page. It is a few kilobytes
    instead of ~38 MB because it downloads the build it installs.

    What it does:
      1. works out which architecture this machine is (x64 or arm64)
      2. finds the latest release and downloads that architecture's zip
      3. extracts it and runs the installer inside it, which copies the build
         to %LOCALAPPDATA%\Programs\WindowsTerminal-RTL and makes the Start menu
         shortcut

    Nothing needs admin rights, the Store terminal is untouched, and your
    settings are picked up automatically from

        %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json

    because a portable build reads the unpackaged settings path.

.PARAMETER InstallDir
    Passed through to the installer inside the zip. Defaults to
    %LOCALAPPDATA%\Programs\WindowsTerminal-RTL.

.PARAMETER NoDesktopShortcut
    Passed through; skip the desktop shortcut.

.PARAMETER Force
    Re-download and reinstall even if the same release appears to be installed.

.EXAMPLE
    .\install-latest.ps1
#>
[CmdletBinding()]
param(
    [string]$InstallDir,
    [switch]$NoDesktopShortcut,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$Owner = 'AmirAliManzar'
$Repo  = 'WindowsTerminal-RTL'
$AppName = 'Windows Terminal RTL'

function Write-Step([string]$msg) { Write-Host "  $msg" }

# ------------------------------------------------------------------ arch

$arch = if ([Environment]::Is64BitOperatingSystem) {
    if ($env:PROCESSOR_ARCHITECTURE -ieq 'ARM64') { 'arm64' } else { 'x64' }
} else { 'x64' }
$assetName = "WindowsTerminal-RTL-$arch.zip"
Write-Host "installing $AppName"
Write-Step "architecture: $arch"

# ------------------------------------------------------------------ installed?

if (-not $Force -and -not $InstallDir) {
    $default = Join-Path $env:LOCALAPPDATA 'Programs\WindowsTerminal-RTL'
    if (Test-Path -LiteralPath (Join-Path $default 'WindowsTerminal.exe')) {
        Write-Step "$AppName is already installed at $default"
        Write-Step "re-run with -Force to reinstall, or -InstallDir to choose elsewhere"
        return
    }
}

# ------------------------------------------------------------------ find release

$api = "https://api.github.com/repos/$Owner/$Repo/releases/latest"
Write-Step "finding the latest release"
$release = Invoke-RestMethod -Uri $api -Headers @{ 'User-Agent' = $AppName } -ErrorAction Stop
Write-Step "latest release: $($release.tag_name)"

$asset = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1
if (-not $asset) { throw "no $assetName in release $($release.tag_name)" }

# ------------------------------------------------------------------ download

$temp = Join-Path ([System.IO.Path]::GetTempPath()) "wt-rtl-install-$([Guid]::NewGuid())"
New-Item -ItemType Directory -Force -Path $temp | Out-Null
try {
    $zip = Join-Path $temp $assetName
    Write-Step "downloading $assetName ($([Math]::Round($asset.size / 1MB, 1)) MB)"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -ErrorAction Stop

    # ------------------------------------------------------------------ verify
    $sumAsset = $release.assets | Where-Object { $_.name -eq "$assetName.sha256" } | Select-Object -First 1
    if ($sumAsset) {
        $sumFile = Join-Path $temp "$assetName.sha256"
        Invoke-WebRequest -Uri $sumAsset.browser_download_url -OutFile $sumFile -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $sumFile) {
            $expected = ((Get-Content $sumFile -Raw) -split '\s+')[0].Trim()
            $actual = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
            if ($expected -ne $actual) { throw "sha256 mismatch: download is corrupt or was tampered with" }
            Write-Step "sha256 verified"
        }
    }

    # ------------------------------------------------------------------ extract + install
    Write-Step "extracting"
    Expand-Archive -LiteralPath $zip -DestinationPath $temp -Force
    $inner = Join-Path $temp 'install.ps1'
    if (-not (Test-Path -LiteralPath $inner)) { throw "the zip has no install.ps1 at its root" }

    Write-Step "installing"
    $installArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $inner)
    if ($InstallDir) { $installArgs += '-InstallDir', $InstallDir }
    if ($NoDesktopShortcut) { $installArgs += '-NoDesktopShortcut' }
    & powershell @installArgs
    if ($LASTEXITCODE -ne 0) { throw "the installer inside the zip exited $LASTEXITCODE" }
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "done. Search the Start menu for `"$AppName`"."
Write-Host "Your settings carry over automatically:"
Write-Host "  %LOCALAPPDATA%\Microsoft\Windows Terminal\settings.json"
