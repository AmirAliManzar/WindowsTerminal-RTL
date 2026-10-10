# Renders a single DIB frame out of a .ico as ASCII art by decoding the raw
# BITMAPINFOHEADER + pixel array directly. .NET's Bitmap constructor refuses
# these standalone DIB blobs, so they have to be walked by hand.
param(
  [Parameter(Mandatory=$true)][string]$Path,
  [int]$Frame = 0,
  [int]$MaxW = 40
)
$bytes = [IO.File]::ReadAllBytes($Path)
$ms = New-Object System.IO.MemoryStream (,$bytes)
$br = New-Object System.IO.BinaryReader $ms
$br.ReadInt16() | Out-Null
$br.ReadInt16() | Out-Null
$count = $br.ReadInt16()
if ($Frame -ge $count) { Write-Host "only $count frames"; exit 1 }

$ms.Position = 6 + ($Frame * 16)
$w = $br.ReadByte(); $h = $br.ReadByte()
$br.ReadByte() | Out-Null; $br.ReadByte() | Out-Null
$br.ReadInt16() | Out-Null
$bpp = $br.ReadInt16()
$sz = $br.ReadInt32()
$off = $br.ReadInt32()
$width = if ($w -eq 0) { 256 } else { $w }
$height = if ($h -eq 0) { 256 } else { $h }
Write-Host "frame $Frame : ${width}x${height}  $bpp bpp"

if ($bpp -ne 32) { Write-Host "  only 32bpp DIBs are supported here ($bpp)"; exit 1 }

# DIB in an .ico: BITMAPINFOHEADER, then height/2 rows of BGRA (the mask half
# follows, but 32bpp icons carry alpha in the pixel half so the mask is ignored).
$ms.Position = $off
$hdrSize = $br.ReadInt32()
$bw = $br.ReadInt32()
$bh = $br.ReadInt32()
$br.ReadInt16() | Out-Null
$planes = $br.ReadInt16()
$bitCount = $br.ReadInt16()
$comp = $br.ReadInt32()
$br.ReadInt32() | Out-Null  # biSizeImage
$br.ReadInt32() | Out-Null  # xPelsPerMeter
$br.ReadInt32() | Out-Null  # yPelsPerMeter
$biClrUsed = $br.ReadInt32()
$br.ReadInt32() | Out-Null  # biClrImportant

$pixelOff = $off + $hdrSize + ($biClrUsed * 4)
$rowSize = [int]([math]::Ceiling($width * 4 / 4)) * 4   # BGRA, already 4-aligned
$masksz = $rowSize * ($bh / 2)   # the AND mask half

# In .ico DIBs the pixel array covers the top half of biHeight; rows are stored
# bottom-up.
$rows = $bh / 2
$pix = New-Object 'int[,]' $width, $rows
for ($y = 0; $y -lt $rows; $y++) {
  $src = ($rows - 1 - $y)
  $ms.Position = $pixelOff + ($src * $rowSize)
  for ($x = 0; $x -lt $width; $x++) {
    $b = $br.ReadByte(); $g = $br.ReadByte(); $r = $br.ReadByte(); $a = $br.ReadByte()
    $pix[$x, ($rows - 1 - $y)] = ($a -shl 24) -bor ($r -shl 16) -bor ($g -shl 8) -bor $b
  }
}

$cw = if ($width -lt $MaxW) { $width } else { $MaxW }
$ch = [int]($rows * $cw / $width)
$ch = if ($ch -lt 1) { 1 } else { $ch }
for ($y = 0; $y -lt $ch; $y++) {
  $line = ''
  for ($x = 0; $x -lt $cw; $x++) {
    $px = $pix[[int]($x * $width / $cw), [int]($y * $rows / $ch)]
    $a = ($px -shr 24) -band 0xFF
    $r = ($px -shr 16) -band 0xFF
    $g = ($px -shr 8) -band 0xFF
    $b = $px -band 0xFF
    if ($a -lt 40)            { $line += '.' }
    elseif ($r -gt 150 -and $g -gt 150) { $line += ':' }
    elseif ($g -gt $r -and $g -gt $b -and $g -gt 100) { $line += 'G' }
    else {
      $lum = [int]($r * 0.3 + $g * 0.59 + $b * 0.11)
      if     ($lum -lt 60)  { $line += '#' }
      elseif ($lum -lt 130) { $line += 'O' }
      elseif ($lum -lt 210) { $line += 'o' }
      else                  { $line += ' ' }
    }
  }
  Write-Host $line
}
