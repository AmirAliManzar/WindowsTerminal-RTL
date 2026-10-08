# Renders a text-art map of an .ico's largest frame so the macro shape can be
# judged without seeing the image. '.' transparent, 'G' badge green, '#' white,
# 'O' light grey, 'o' mid grey, ':' dark grey, ' ' near black.
param([Parameter(Mandatory)][string]$Path, [int]$Grid = 40)

Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $Path
$W = $bmp.Width; $H = $bmp.Height
Write-Host "size: ${W}x${H}"

$cellW = [int]([Math]::Floor($W / $Grid))
$cellH = [int]([Math]::Floor($H / $Grid))

for ($gy = 0; $gy -lt $Grid; $gy++) {
    $sb = New-Object System.Text.StringBuilder
    for ($gx = 0; $gx -lt $Grid; $gx++) {
        $sumR = 0; $sumG = 0; $sumB = 0; $n = 0
        for ($y = $gy * $cellH; $y -lt ($gy + 1) * $cellH; $y++) {
            for ($x = $gx * $cellW; $x -lt ($gx + 1) * $cellW; $x++) {
                $c = $bmp.GetPixel($x, $y)
                if ($c.A -lt 128) { continue }
                $sumR += $c.R; $sumG += $c.G; $sumB += $c.B; $n++
            }
        }
        if ($n -eq 0) { [void]$sb.Append('.'); continue }
        $r = $sumR / $n; $g = $sumG / $n; $b = $sumB / $n
        $max = [Math]::Max($r, [Math]::Max($g, $b))
        $min = [Math]::Min($r, [Math]::Min($g, $b))
        $bright = $max / 255
        if ($g -gt 170 -and $r -lt 160 -and $b -lt 70) { [void]$sb.Append('G') }    # badge green
        elseif (($b - $r) -gt 45 -and ($b - $g) -gt 30) { [void]$sb.Append('A') }    # blue arrow
        elseif ($bright -gt 0.85) { [void]$sb.Append('#') }                          # near white
        elseif ($bright -gt 0.55) { [void]$sb.Append('O') }                          # light grey
        elseif ($bright -gt 0.25) { [void]$sb.Append('o') }                          # mid grey
        elseif ($bright -gt 0.10) { [void]$sb.Append(':') }                          # dark grey
        else { [void]$sb.Append(' ') }                                              # near black
    }
    Write-Host $sb.ToString()
}
$bmp.Dispose()
