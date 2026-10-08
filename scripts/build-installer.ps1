<#
.SYNOPSIS
    Builds the WindowsTerminal-RTL installer EXE.

.DESCRIPTION
    The user asked for a real installer executable rather than a PowerShell
    script, so this compiles installer/Program.cs with csc.exe.

    Deliberately no SDK-style project, no dotnet, no MSBuild and no NuGet: the
    whole point of a standalone installer is that it cannot depend on a build
    toolchain that may or may not be on the machine running this script. csc.exe
    and the reference assemblies are part of the .NET Framework that ships with
    every Windows 10/11 box, and that is all this needs.

    The resulting EXE is a console application that embeds the RTL icon, needs
    no admin rights, and carries the terminal's version number.

.PARAMETER OutDir
    Where to write WindowsTerminal-RTL-Installer.exe. Defaults to a new
    "installer" folder beside the script's parent.

.EXAMPLE
    .\build-installer.ps1 -OutDir ..\artifacts
#>
[CmdletBinding()]
param(
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# ------------------------------------------------------------------ inputs

$source  = @(
    (Join-Path $repoRoot 'installer\Program.cs'),
    (Join-Path $repoRoot 'installer\InstallJob.cs'),
    (Join-Path $repoRoot 'installer\Strings.cs'),
    (Join-Path $repoRoot 'installer\WizardForm.cs'),
    (Join-Path $repoRoot 'installer\Properties\AssemblyInfo.cs')
)
$icon    = Join-Path $repoRoot 'assets\installer-rtl.ico'

foreach ($f in (@($source) + @($icon))) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing input: $f" }
}

if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'installer\out' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = Join-Path $OutDir 'WindowsTerminal-RTL-Installer.exe'

# ------------------------------------------------------------------ csc

# Prefer the 64-bit Framework csc.exe. The reference assemblies are resolved
# explicitly below, so the only thing needed from this directory is the
# compiler itself.
$fw = 'C:\Windows\Microsoft.NET\Framework64'
if (-not (Test-Path $fw)) { $fw = 'C:\Windows\Microsoft.NET\Framework' }
$csc = Get-ChildItem $fw -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'csc.exe') } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $csc) { throw "no csc.exe under $fw" }
$csc = Join-Path $csc.FullName 'csc.exe'
Write-Host "  csc    : $csc"

# csc.exe reads csc.rsp next to itself, which already references mscorlib,
# System, System.Core, System.Web.Extensions, Microsoft.CSharp and the rest of
# the BCL. Naming those again is a CS1703 duplicate-import error, so the only
# things named explicitly here are the ones the response file leaves out, and
# they are resolved from csc's own directory so the search order cannot pick a
# stale copy from elsewhere on the machine.
$extraRefs = @(
    'System.Net.Http.dll',
    'System.IO.Compression.dll',
    'System.IO.Compression.FileSystem.dll',
    'System.Windows.Forms.dll',
    'System.Drawing.dll'
) | ForEach-Object { "/reference:" + (Join-Path (Split-Path -Parent $csc) $_) }

foreach ($r in $extraRefs) {
    $p = $r -replace '^/reference:', ''
    if (-not (Test-Path -LiteralPath $p)) { throw "missing reference: $p" }
}

# ------------------------------------------------------------------ compile

# /target:winexe  a GUI app: double-clicked it opens the wizard and never
#                 flashes a console window. Run from an existing shell it still
#                 writes its progress to that shell, so --uninstall stays
#                 scriptable.
# /platform:anycpu the installer runs on x64 and on ARM64 Windows. The build
#                 it downloads is chosen per machine, so the installer itself
#                 must not be pinned to one.
# /resource       embeds the RTL icon so the wizard can show it at full size
#                 in its header, not just as the 16px taskbar glyph.
# /win32icon   embeds the RTL badge so the EXE shows it in the taskbar, in
#              Explorer and in the Start menu, instead of the generic one.
# /win32manifest keeps it asInvoker and puts it on the modern common controls.
#
# No /langversion: the csc.exe that ships in the .NET Framework directory is
# the C# 5 compiler, which does not know 7.3. The installer deliberately stays
# inside C# 5 syntax for exactly that reason, so the default is correct.
#
# The switch strings are precomputed into variables rather than built inline in
# the array literal. Inside an array literal the unary comma binds tighter than
# "+", so @('/x:' + $path) is three elements, not two, and csc sees a bare
# '/x:' followed by a stray path and dies with CS2005.
$manifestArg = "/win32manifest:" + (Join-Path $repoRoot 'installer\app.manifest')
$outArg      = "/out:" + $out
$iconArg     = "/win32icon:" + $icon
$iconResArg  = "/resource:" + $icon + ",installer-rtl.ico"

$cscArgs = @(
    '/nologo', '/target:winexe', '/platform:anycpu',
    $outArg, $iconArg, $iconResArg, $manifestArg,
    '/checked-',
    '/filealign:512'
) + $extraRefs + $source

Write-Host "  compile: WindowsTerminal-RTL-Installer.exe"
& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "csc.exe exited $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $out)) { throw "csc.exe produced no output" }

$size = (Get-Item -LiteralPath $out).Length
Write-Host "  out    : $out ($size bytes)"
return $out
