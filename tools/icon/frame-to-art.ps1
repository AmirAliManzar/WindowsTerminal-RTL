# Draws a specific frame out of a .ico file as ASCII art, so the small taskbar
# sizes (16x16, 24x24, 32x32) can be inspected instead of only the 256 one.
param(
  [Parameter(Mandatory=$true)][string]$Path,
  [int]$Frame = 0
)
Add-Type -AssemblyName System.Drawing
$bytes = [IO.File]::ReadAllBytes($Path)
$ms = New-Object System.IO.MemoryStream (,$bytes)
$br = New-Object System.IO.BinaryReader $ms
$reserved = $br.ReadInt16()
$type = $br.ReadInt16()
$count = $br.ReadInt16()
if ($type -ne 1) { Write-Host 'not an icon file (type=' $type ')'; exit 1 }
if ($Frame -ge $count) { Write-Host "only $count frames, asked for $Frame"; exit 1 }
$br.BaseStream.Position = 6 + ($Frame * 16)
$w = $br.ReadByte(); $h = $br.ReadByte()
$cc = $br.ReadByte(); $br.ReadByte() > $null
$planes = $br.ReadInt16(); $bpp = $br.ReadInt16()
$size = $br.ReadInt32(); $off = $br.ReadInt32()
$width = if ($w -eq 0) { 256 } else { $w }
$height = if ($h -eq 0) { 256 } else { $h }
Write-Host "frame $Frame : ${width}x${height}  $bpp bpp  $size bytes"
$br.BaseStream.Position = $off
$png = $br.ReadBytes($size)
$tmp = [IO.Path]::GetTempFileName() + '.png'
[IO.File]::WriteAllBytes($tmp, $png)
try {
  $bmp = New-Object System.Drawing.Bitmap $tmp
  $cw = if ($width -gt 40) { 40 } else { $width }
  $ch = if ($height -gt 24) { [int]($height * 40 / $width) } else { $height }
  for ($y = 0; $y -lt $ch; $y++) {
    $line = ''
    for ($x = 0; $x -lt $cw; $x++) {
      $px = $bmp.GetPixel([int]($x * $width / $cw), [int]($y * $height / $ch))
      $lum = [int]($px.R * 0.3 + $px.G * 0.59 + $px.B * 0.11)
      $a = $px.A
      if ($a -lt 32)   { $line += '.' }
      elseif ($lum -lt 60)  { $line += '#' }
      elseif ($lum -lt 130) { $line += 'O' }
      elseif ($lum -lt 210) { $line += 'o' }
      elseif ($px.G -gt $px.R -and $px.G -gt $px.B -and $px.G -gt 110) { $line += 'G' }
      else { $line += ':' }
    }
    Write-Host $line
  }
  $bmp.Dispose()
} catch {
  Write-Host "could not decode frame $Frame"
}
Remove-Item $tmp -Force -ErrorAction SilentlyContinue
