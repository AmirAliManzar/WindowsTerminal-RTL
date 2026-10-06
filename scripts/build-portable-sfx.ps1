<#
.SYNOPSIS
    Packs a packaged portable build into one self-extracting EXE.

.DESCRIPTION
    The whole build delivered as a single file per architecture: one EXE that is
    the product, runnable from anywhere with no installer. This script is what
    produces that file.

    It zips the packaged portable folder, pairs that payload with a version
    marker read from the installer's AssemblyInfo, and compiles
    portable/PortableLauncher.cs around both. The launcher embeds the payload as
    a resource and extracts it on first run, so the EXE is genuinely the whole
    product: terminal, DLLs and the Cascadia fonts it renders with.

    Deliberately no SDK-style project, no dotnet, no MSBuild and no NuGet: csc.exe
    is part of the .NET Framework that ships with every Windows 10/11 machine, and
    that is all this needs. It is also the reason the launcher source is held to
    C# 5 syntax.

.PARAMETER PayloadDir
    The packaged portable folder for one architecture, the one that already
    contains WindowsTerminal.exe.

.PARAMETER Arch
    The architecture of that folder. Baked into the version marker so payloads
    for different architectures never overwrite each other.

.PARAMETER OutDir
    Where to write the self-extracting EXE. Defaults to a "portable/out" folder
    inside the repo.

.EXAMPLE
    .\build-portable-sfx.ps1 -PayloadDir ..\packaged\WindowsTerminal-RTL-x64 -Arch x64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadDir,

    [Parameter(Mandatory = $true)]
    [string]$Arch,

    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# ------------------------------------------------------------------ inputs

$source      = Join-Path $repoRoot 'portable\PortableLauncher.cs'
$manifest    = Join-Path $repoRoot 'portable\app.manifest'
$icon        = Join-Path $repoRoot 'assets\terminal-rtl.ico'
$assemblyInfo = Join-Path $repoRoot 'installer\Properties\AssemblyInfo.cs'

foreach ($f in @($source, $manifest, $icon, $assemblyInfo)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing input: $f" }
}

if (-not (Test-Path -LiteralPath $PayloadDir)) {
    throw "PayloadDir does not exist: $PayloadDir"
}
$payloadExe = Join-Path $PayloadDir 'WindowsTerminal.exe'
if (-not (Test-Path -LiteralPath $payloadExe)) {
    throw "PayloadDir has no WindowsTerminal.exe in it, so there is nothing to pack: $PayloadDir"
}

# The version travels into the marker, which is what separates one release from
# the next on the user's disk. Reading it from the installer's AssemblyInfo keeps
# the portable build and the wizard on the same number.
$versionLine = Get-Content -LiteralPath $assemblyInfo |
    Where-Object { $_ -match 'AssemblyVersion\(' } |
    Select-Object -First 1
if (-not $versionLine) { throw "cannot read AssemblyVersion from $assemblyInfo" }
$version = $versionLine -replace '.*AssemblyVersion\("([^"]+)".*', '$1'
if (-not $version) { throw "could not parse a version out of: $versionLine" }
Write-Host "  version: $version  arch: $Arch"

if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'portable\out' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$out = Join-Path $OutDir ("WindowsTerminal-RTL-Portable-" + $Arch + ".exe")

# ------------------------------------------------------------------ payload

$work = Join-Path $env:TEMP ("wt-rtl-sfx-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$zip        = Join-Path $work 'payload.zip'
$markerPath = Join-Path $work 'marker.txt'

try {
    # includeBaseDirectory is false, so the entries are relative to the payload
    # folder itself and WindowsTerminal.exe lands directly at the root of the
    # extraction target.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $PayloadDir, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)

    # A short hash of the payload, not just the version, is what makes a re-shipped
    # build of the same version re-extract: the marker no longer matches, so the
    # launcher replaces the folder instead of trusting stale files.
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    $marker = "$version $Arch $($hash.Substring(0, 16))"
    [IO.File]::WriteAllText($markerPath, $marker, [Text.Encoding]::ASCII)

    $zipSize = (Get-Item -LiteralPath $zip).Length
    Write-Host "  payload: $zipSize bytes zipped"

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
    # System, System.Core and the rest of the BCL. Naming those again is a CS1703
    # duplicate-import error, so the only thing named explicitly is what the
    # response file leaves out, resolved from csc's own directory so the search
    # order cannot pick a stale copy from elsewhere on the machine.
    $extraRefs = @(
        'System.IO.Compression.dll'
    ) | ForEach-Object { "/reference:" + (Join-Path (Split-Path -Parent $csc) $_) }

    foreach ($r in $extraRefs) {
        $p = $r -replace '^/reference:', ''
        if (-not (Test-Path -LiteralPath $p)) { throw "missing reference: $p" }
    }

    # /target:winexe  a GUI app: double-clicked it never flashes a console window
    #                 while it extracts, and errors surface in a message box.
    # /platform:anycpu the launcher runs on x64 and on ARM64 Windows and then
    #                 starts the architecture-specific terminal it carried inside.
    # /win32icon   the RTL badge, so the EXE shows it in Explorer, in the taskbar
    #              and in the Start menu rather than the generic icon.
    # /win32manifest asInvoker plus comctl 6, matching the installer: no UAC
    #                prompt, modern visual styles.
    # /resource   embeds the payload and the marker. The name after the comma is
    #             the exact manifest resource name, so the launcher can find them.
    #
    # No /langversion: the csc.exe in the .NET Framework directory is the C# 5
    # compiler. The launcher source stays inside C# 5 syntax for that reason, so
    # the default is correct.
    #
    # The switch strings are precomputed into variables rather than built inline in
    # the array literal. Inside an array literal the unary comma binds tighter than
    # "+", so @('/x:' + $path) is three elements, not two, and csc sees a bare
    # '/x:' followed by a stray path and dies with CS2005.
    $outArg     = "/out:" + $out
    $iconArg    = "/win32icon:" + $icon
    $manifestArg = "/win32manifest:" + $manifest
    $payloadRes = "/resource:" + $zip + ",payload.zip"
    $markerRes  = "/resource:" + $markerPath + ",marker.txt"

    $cscArgs = @(
        '/nologo', '/target:winexe', '/platform:anycpu',
        $outArg, $iconArg, $manifestArg,
        $payloadRes, $markerRes,
        '/checked-',
        '/filealign:512'
    ) + $extraRefs + @($source)

    Write-Host "  compile: $(Split-Path -Leaf $out)"
    & $csc @cscArgs
    if ($LASTEXITCODE -ne 0) { throw "csc.exe exited $LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $out)) { throw "csc.exe produced no output" }

    # The payload is embedded whole and uncompressed inside the EXE, so the output
    # must be larger than the zip it came from. If it is not, the resource was
    # silently dropped and the launcher would have nothing to extract.
    $exeSize = (Get-Item -LiteralPath $out).Length
    if ($exeSize -le $zipSize) {
        throw "the launcher ($exeSize bytes) is not larger than its payload ($zipSize bytes), so the embedded resource is missing"
    }

    Write-Host "  out    : $out ($exeSize bytes)"
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

return $out
