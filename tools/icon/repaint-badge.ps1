<#
.SYNOPSIS
    Repaints the "DEV" badge on the upstream terminal icon as "RTL".

.DESCRIPTION
    Takes res/terminal/images-Dev/terminal.ico from microsoft/terminal and
    rewrites only the badge: the green plate is found by colour, the DEV letters
    are erased back to that green, and "RTL" is drawn in the same dark ink. Every
    other pixel of the original icon is left alone, so the result is the real
    Windows Terminal icon with one word changed.

    The frame encoding is preserved byte for byte in structure: the small sizes
    stay DIBs (BITMAPINFOHEADER + XOR mask + AND mask) and the 256 stays PNG. The
    AND mask of each DIB is copied through untouched rather than regenerated.
#>
param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$OutFile
)

Add-Type -AssemblyName System.Drawing

$Green = [System.Drawing.Color]::FromArgb(155, 240, 11)
$Ink   = [System.Drawing.Color]::FromArgb(39, 60, 3)

$bytes = [System.IO.File]::ReadAllBytes($Source)
$cnt   = [BitConverter]::ToUInt16($bytes, 4)

# ---------------------------------------------------------------- DIB decode

# A bare DIB (BITMAPINFOHEADER + pixels, no file header). GDI+ will not read this
# shape directly, so the header is parsed and the scanlines copied by hand.
function ConvertFrom-Dib([byte[]]$dib, [int]$expectedW) {
    $biSize      = [BitConverter]::ToUInt32($dib, 0)
    $biWidth     = [BitConverter]::ToInt32($dib, 4)
    $biHeightRaw = [BitConverter]::ToInt32($dib, 8)
    $biBitCount  = [BitConverter]::ToUInt16($dib, 14)
    if ($biSize -lt 40) { throw "unexpected BITMAPINFOHEADER size $biSize" }
    if ($biBitCount -ne 32) { throw "only 32bpp DIBs are handled, got $biBitCount bpp" }

    # In an .ico the DIB's biHeight counts the AND mask as well, so it is twice
    # the real height; the pixel data only covers the first half.
    $h = [Math]::Abs($biHeightRaw)
    if (($h % 2) -eq 0) { $h = $h / 2 }

    $w = $biWidth
    $stride = $w * 4
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $o = $biSize + ($h - 1 - $y) * $stride + $x * 4
            $b = $dib[$o]; $g = $dib[$o + 1]; $r = $dib[$o + 2]; $a = $dib[$o + 3]
            $bmp.SetPixel($x, $y, ([System.Drawing.Color]::FromArgb($a, $r, $g, $b)))
        }
    }
    return $bmp
}

# ---------------------------------------------------------------- badge repaint

function Repaint-Badge([System.Drawing.Bitmap]$bmp) {
    $S = $bmp.Width
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    # Find the badge: a block of saturated green in the bottom-right quadrant.
    $minX = $S; $minY = $S; $maxX = 0; $maxY = 0
    for ($y = 0; $y -lt $S; $y++) {
        for ($x = 0; $x -lt $S; $x++) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.A -lt 200) { continue }
            if ($c.G -gt 170 -and $c.R -lt 160 -and $c.B -lt 70 -and ($c.G - $c.R) -gt 60) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -le $minX) { $g.Dispose(); return $false }

    # Erase the DEV letters by repainting the plate, then write RTL in the same
    # ink the original used.
    $pad = [Math]::Max(1, [int]($S * 0.03))
    $rx = $minX - $pad; $ry = $minY - $pad
    $rw = ($maxX - $minX) + 2 * $pad; $rh = ($maxY - $minY) + 2 * $pad
    $g.FillRectangle((New-Object System.Drawing.SolidBrush $Green), $rx, $ry, $rw, $rh)

    if ($rh -ge 12) {
        $em = $rh * 0.78
        $font = New-Object System.Drawing.Font "Consolas", $em, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment     = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $rect = New-Object System.Drawing.RectangleF $rx, $ry, $rw, $rh
        $g.DrawString("RTL", $font, (New-Object System.Drawing.SolidBrush $Ink), $rect, $sf)
        $font.Dispose()
    } else {
        # Too small for three letters: a down-arrow, the direction RTL runs.
        $pw = [Math]::Max(1.5, ($S * 0.09))
        $pen = New-Object System.Drawing.Pen $Ink, $pw
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $cx = $rx + $rw / 2
        $g.DrawLine($pen, (New-Object System.Drawing.PointF ($cx, ($ry + $rh * 0.22))), (New-Object System.Drawing.PointF ($cx, ($ry + $rh * 0.72))))
        $g.DrawLines($pen, @(
            (New-Object System.Drawing.PointF (($cx - $rw * 0.24), ($ry + $rh * 0.52)))
            (New-Object System.Drawing.PointF ($cx, ($ry + $rh * 0.75)))
            (New-Object System.Drawing.PointF (($cx + $rw * 0.24), ($ry + $rh * 0.52)))
        ))
    }
    $g.Dispose()
    return $true
}

# ---------------------------------------------------------------- run

$frames = @()
for ($i = 0; $i -lt $cnt; $i++) {
    $o   = 6 + 16 * $i
    $w   = $bytes[$o]
    $sz  = [BitConverter]::ToUInt32($bytes, $o + 8)
    $off = [BitConverter]::ToUInt32($bytes, $o + 12)
    $W   = if ($w -eq 0) { 256 } else { $w }
    $raw = $bytes[$off..($off + $sz - 1)]
    $isPng = ($raw[0] -eq 0x89 -and $raw[1] -eq 0x50)

    if ($isPng) {
        $ms  = New-Object System.IO.MemoryStream (,$raw)
        $bmp = [System.Drawing.Bitmap]::FromStream($ms)
        $ms.Dispose()
        [void](Repaint-Badge $bmp)
        $out = New-Object System.IO.MemoryStream
        $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $frames += [PSCustomObject]@{ Size = $W; Data = $out.ToArray() }
        $out.Dispose()
    } else {
        $bmp = ConvertFrom-Dib $raw $W
        [void](Repaint-Badge $bmp)
        # Rebuild the XOR mask from the repainted bitmap and carry the original
        # AND mask through verbatim: it is still an accurate transparency map and
        # regenerating it was the part that kept throwing.
        $biSize = [BitConverter]::ToUInt32($raw, 0)
        $w2 = $bmp.Width; $h2 = $bmp.Height
        $stride = $w2 * 4
        $xor = New-Object byte[] ($stride * $h2)
        for ($y = 0; $y -lt $h2; $y++) {
            for ($x = 0; $x -lt $w2; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $o = ($h2 - 1 - $y) * $stride + $x * 4
                $xor[$o]     = $c.B
                $xor[$o + 1] = $c.G
                $xor[$o + 2] = $c.R
                $xor[$o + 3] = $c.A
            }
        }
        $andLen = $raw.Length - $biSize - $xor.Length
        if ($andLen -lt 0) { throw "frame $W : DIB is shorter than header + XOR mask" }
        $and = $raw[($biSize + $xor.Length)..($raw.Length - 1)]
        $hdr = New-Object byte[] 40
        [BitConverter]::GetBytes([UInt32]40).CopyTo($hdr, 0)
        [BitConverter]::GetBytes([Int32]$w2).CopyTo($hdr, 4)
        [BitConverter]::GetBytes([Int32](2 * $h2)).CopyTo($hdr, 8)
        [BitConverter]::GetBytes([UInt16]1).CopyTo($hdr, 12)
        [BitConverter]::GetBytes([UInt16]32).CopyTo($hdr, 14)
        [BitConverter]::GetBytes([UInt32]0).CopyTo($hdr, 16)
        [BitConverter]::GetBytes([UInt32]($xor.Length + $and.Length)).CopyTo($hdr, 20)
        $bmp.Dispose()

        $ms = New-Object System.IO.MemoryStream
        $ms.Write($hdr, 0, 40)
        $ms.Write($xor, 0, $xor.Length)
        $ms.Write($and, 0, $and.Length)
        $frames += [PSCustomObject]@{ Size = $W; Data = $ms.ToArray() }
        $ms.Dispose()
    }
}

# ---------------------------------------------------------------- write .ico

$fs = [System.IO.File]::Create($OutFile)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$frames.Count)
$offset = 6 + 16 * $frames.Count
for ($i = 0; $i -lt $frames.Count; $i++) {
    $f = $frames[$i]
    $w = if ($f.Size -eq 256) { [Byte]0 } else { [Byte]$f.Size }
    $bw.Write([Byte]$w)
    $bw.Write([Byte]$w)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$f.Data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $f.Data.Length
}
foreach ($f in $frames) { $bw.Write($f.Data) }
$bw.Flush(); $bw.Close(); $fs.Close()

"wrote $OutFile ($((Get-Item $OutFile).Length) bytes, $($frames.Count) frames)"
