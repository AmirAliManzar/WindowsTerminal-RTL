<#
.SYNOPSIS
    Generates assets/installer-rtl.ico: the installer's own download-themed icon.

.DESCRIPTION
    The terminal's icon is a terminal device with an RTL badge. The installer is a
    different program and gets a different picture: a download box (an open tray or
    package) with a down arrow landing in it, plus the same green RTL badge in the
    same corner, drawn with the same green and ink as the terminal badge so the two
    icons read as one family.

    Frames are the standard 7 (16,20,24,32,48,64 as 32bpp DIBs, 256 as PNG) so the
    EXE looks right at every Explorer/taskbar size. Everything is drawn, so no
    source artwork is needed.
#>
param(
    [Parameter(Mandatory)][string]$OutFile
)

Add-Type -AssemblyName System.Drawing

# The same palette the terminal badge uses, so the two icons are one family.
$Green   = [System.Drawing.Color]::FromArgb(155, 240, 11)
$Ink     = [System.Drawing.Color]::FromArgb(39, 60, 3)
$Body    = [System.Drawing.Color]::FromArgb(216, 220, 224)   # light case
$BodyDark = [System.Drawing.Color]::FromArgb(148, 156, 164)  # case shading
$Screen  = [System.Drawing.Color]::FromArgb(238, 242, 246)   # tray interior
$Arrow   = [System.Drawing.Color]::FromArgb(66, 110, 220)    # download arrow

function Draw-InstallerIcon([int]$S) {
    $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    $u = $S / 256.0   # design unit; the drawing below is authored on a 256 grid

    # ---------------------------------------------- rounded square case
    $casePad  = 30 * $u
    $caseW    = ($S - 2 * $casePad)
    $caseH    = ($S - 2 * $casePad)
    $r        = 42 * $u
    $caseRect = New-Object System.Drawing.RectangleF $casePad, $casePad, $caseW, $caseH
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($caseRect.X, $caseRect.Y, $r, $r, 180, 90)
    $path.AddArc($caseRect.Right - $r, $caseRect.Y, $r, $r, 270, 90)
    $path.AddArc($caseRect.Right - $r, $caseRect.Bottom - $r, $r, $r, 0, 90)
    $path.AddArc($caseRect.X, $caseRect.Bottom - $r, $r, $r, 90, 90)
    $path.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush $Body), $path)

    # top sheen: a lighter band across the top of the case
    $sheenH = 26 * $u
    $sheen = New-Object System.Drawing.Drawing2D.GraphicsPath
    $sheen.AddArc($caseRect.X, $caseRect.Y, $r, $r, 180, 90)
    $sheen.AddLine(($caseRect.Right - $r), $caseRect.Y, $caseRect.Right, ($caseRect.Y + $r))
    $sheen.AddLine($caseRect.Right, ($caseRect.Y + $sheenH), $caseRect.X, ($caseRect.Y + $sheenH))
    $sheen.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush $Screen), $sheen)

    # ---------------------------------------------- open tray in the middle
    # Kept above the badge so the two never overlap.
    $trayY    = (256 * 0.50) * $u
    $trayH    = (256 * 0.155) * $u
    $trayX    = $casePad + 34 * $u
    $trayW    = $caseW - 68 * $u
    $trayRect = New-Object System.Drawing.RectangleF $trayX, $trayY, $trayW, $trayH
    $g.FillRectangle((New-Object System.Drawing.SolidBrush $Screen), $trayRect)
    # tray rim
    $rim = New-Object System.Drawing.Pen $BodyDark, (4 * $u)
    $g.DrawRectangle($rim, $trayRect.X, $trayRect.Y, $trayRect.Width, $trayRect.Height)

    # ---------------------------------------------- down arrow
    $cx = 128 * $u
    $arrowTop = (256 * 0.20) * $u
    $arrowBottom = $trayY - 8 * $u
    $shaftW = (256 * 0.115) * $u
    $headW  = (256 * 0.245) * $u
    $headH  = (256 * 0.135) * $u

    $shaft = New-Object System.Drawing.RectangleF (($cx - $shaftW / 2), $arrowTop, $shaftW, ($arrowBottom - $arrowTop - $headH * 0.55))
    $g.FillRectangle((New-Object System.Drawing.SolidBrush $Arrow), $shaft)

    $head = New-Object System.Drawing.Drawing2D.GraphicsPath
    $head.AddPolygon(@(
        (New-Object System.Drawing.PointF (($cx - $headW / 2), ($arrowBottom - $headH)))
        (New-Object System.Drawing.PointF ($cx, $arrowBottom))
        (New-Object System.Drawing.PointF (($cx + $headW / 2), ($arrowBottom - $headH)))
        (New-Object System.Drawing.PointF (($cx + $shaftW / 2), ($arrowBottom - $headH)))
        (New-Object System.Drawing.PointF (($cx + $shaftW / 2), ($arrowBottom - $headH * 0.52)))
        (New-Object System.Drawing.PointF (($cx - $shaftW / 2), ($arrowBottom - $headH * 0.52)))
        (New-Object System.Drawing.PointF (($cx - $shaftW / 2), ($arrowBottom - $headH)))
    ))
    $g.FillPath((New-Object System.Drawing.SolidBrush $Arrow), $head)

    # ---------------------------------------------- RTL badge, bottom-right
    # The badge is a green plate inset into the corner of the case, sized so it
    # never collides with the tray. Letters shrink to a down arrow below 48px.
    $bw = (256 * 0.40) * $u
    $bh = (256 * 0.155) * $u
    $bx = $casePad + $caseW - $bw - 10 * $u
    $by = $casePad + $caseH - $bh - 10 * $u
    $badge = New-Object System.Drawing.RectangleF $bx, $by, $bw, $bh
    $g.FillRectangle((New-Object System.Drawing.SolidBrush $Green), $badge)

    if ($S -ge 48) {
        $em = $bh * 0.80
        $font = New-Object System.Drawing.Font "Consolas", $em, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $sf = New-Object System.Drawing.StringFormat
        $sf.Alignment     = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
        $g.DrawString("RTL", $font, (New-Object System.Drawing.SolidBrush $Ink), $badge, $sf)
        $font.Dispose()
    } else {
        $pw = [Math]::Max(1.5, ($S * 0.09))
        $pen = New-Object System.Drawing.Pen $Ink, $pw
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $cx2 = $bx + $bw / 2
        $g.DrawLine($pen, (New-Object System.Drawing.PointF ($cx2, ($by + $bh * 0.22))), (New-Object System.Drawing.PointF ($cx2, ($by + $bh * 0.72))))
        $g.DrawLines($pen, @(
            (New-Object System.Drawing.PointF (($cx2 - $bw * 0.24), ($by + $bh * 0.52)))
            (New-Object System.Drawing.PointF ($cx2, ($by + $bh * 0.75)))
            (New-Object System.Drawing.PointF (($cx2 + $bw * 0.24), ($by + $bh * 0.52)))
        ))
    }

    $g.Dispose()
    return $bmp
}

# ---------------------------------------------------------------- build frames
$sizes = @(16, 20, 24, 32, 48, 64, 256)
$frames = @()
foreach ($S in $sizes) {
    $bmp = Draw-InstallerIcon $S
    if ($S -eq 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $frames += [PSCustomObject]@{ Size = $S; Data = $ms.ToArray() }
        $ms.Dispose()
    } else {
        # Build a bare DIB (BITMAPINFOHEADER + XOR + AND mask), matching how the
        # terminal icon encodes its small frames.
        $w = $bmp.Width; $h = $bmp.Height
        $stride = $w * 4
        $xor = New-Object byte[] ($stride * $h)
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $c = $bmp.GetPixel($x, $y)
                $o = ($h - 1 - $y) * $stride + $x * 4
                $xor[$o]     = $c.B
                $xor[$o + 1] = $c.G
                $xor[$o + 2] = $c.R
                $xor[$o + 3] = $c.A
            }
        }
        # AND mask: 1bpp, 1 = transparent. Aligned to 4 bytes per row.
        $rowBytes = [int][Math]::Ceiling($w / 8.0)
        $andRow = [int][Math]::Ceiling($rowBytes / 4.0) * 4
        $and = New-Object byte[] ($andRow * $h)
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $c = $bmp.GetPixel($x, $y)
                if ($c.A -ge 128) { continue }
                $byteIdx = ($h - 1 - $y) * $andRow + ($x -shr 3)
                $and[$byteIdx] = $and[$byteIdx] -bor ([byte](0x80 -shr ($x -band 7)))
            }
        }
        $hdr = New-Object byte[] 40
        [BitConverter]::GetBytes([UInt32]40).CopyTo($hdr, 0)
        [BitConverter]::GetBytes([Int32]$w).CopyTo($hdr, 4)
        [BitConverter]::GetBytes([Int32](2 * $h)).CopyTo($hdr, 8)
        [BitConverter]::GetBytes([UInt16]1).CopyTo($hdr, 12)
        [BitConverter]::GetBytes([UInt16]32).CopyTo($hdr, 14)
        [BitConverter]::GetBytes([UInt32]0).CopyTo($hdr, 16)
        [BitConverter]::GetBytes([UInt32]($xor.Length + $and.Length)).CopyTo($hdr, 20)

        $ms = New-Object System.IO.MemoryStream
        $ms.Write($hdr, 0, 40)
        $ms.Write($xor, 0, $xor.Length)
        $ms.Write($and, 0, $and.Length)
        $frames += [PSCustomObject]@{ Size = $S; Data = $ms.ToArray() }
        $ms.Dispose()
    }
    $bmp.Dispose()
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
