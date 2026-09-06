# Build the weapon wheel icons from the Quake II data already on this machine.
#
# A port of make-wheel-icons.py with no dependencies at all - PowerShell and
# System.Drawing are both part of Windows, so nothing has to be installed. The
# maths is that script's, unchanged, and the two are verified against each other
# by generating every icon both ways and comparing pixels.
#
# Team Beef's wheel art covers baseq2's weapons and items. The Reckoning and
# Ground Zero add more and nothing ships art for those - but their HUD icons are
# in the packs' own paks at exactly the 24x24 the wheel draws, and Team Beef's
# w_railgun.png IS baseq2's pics/w_railgun.pcx unchanged. So the same icons,
# taken from the paks the player already owns, are the right source and nothing
# needs redistributing.

Add-Type -AssemblyName System.Drawing

function PathJoin([string]$a, [string]$b) {
    # Deliberately not Join-Path. That is a provider cmdlet: it validates the
    # drive and throws DriveNotFoundException for a path on a drive this machine
    # does not have. The install guesses are full of exactly that - D: and E:
    # exist on the machine this was written on and not on the one it was tested
    # on, where Setup died on the first guess it could not resolve.
    #
    # This was ported from Python, where os.path.join is pure string handling and
    # never touches the filesystem. Combine is the equivalent; Join-Path is not.
    return [System.IO.Path]::Combine($a, $b)
}

# An image here is width, height and a BGRA byte array, which is the order
# System.Drawing wants in memory. Pillow works in RGBA, so anything lifted from
# the Python has its channels swapped on the way in and out.
function New-Img([int]$w, [int]$h) {
    [pscustomobject]@{ W = $w; H = $h; P = (New-Object byte[] ($w * $h * 4)) }
}

function Save-Img($img, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($img.W, $img.H,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $img.W, $img.H)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    # Stride can exceed width*4, so copy row by row rather than in one go.
    for ($y = 0; $y -lt $img.H; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy(
            $img.P, $y * $img.W * 4,
            [IntPtr]::Add($data.Scan0, $y * $data.Stride), $img.W * 4)
    }
    $bmp.UnlockBits($data)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# --- pak reading ----------------------------------------------------------

function Get-PakIndex([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $magic = $br.ReadBytes(4)
        if ([System.Text.Encoding]::ASCII.GetString($magic) -ne 'PACK') {
            throw "$path is not a pak file"
        }
        $ofs = $br.ReadInt32()
        $len = $br.ReadInt32()
        $fs.Position = $ofs
        $index = @{}
        for ($i = 0; $i -lt [int]($len / 64); $i++) {
            $rec = $br.ReadBytes(64)
            $z = [Array]::IndexOf($rec, [byte]0, 0, 56)
            if ($z -lt 0) { $z = 56 }
            $name = [System.Text.Encoding]::GetEncoding(28591).GetString($rec, 0, $z).ToLower()
            $index[$name] = @([BitConverter]::ToInt32($rec, 56), [BitConverter]::ToInt32($rec, 60))
        }
        return $index
    } finally { $fs.Close() }
}

function Read-PakFile([string]$path, $index, [string]$name) {
    $key = $name.ToLower()
    if (-not $index.ContainsKey($key)) { return $null }
    $e = $index[$key]
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $fs.Position = $e[0]
        $buf = New-Object byte[] $e[1]
        [void]$fs.Read($buf, 0, $e[1])
        return $buf
    } finally { $fs.Close() }
}

# --- pcx decoding ---------------------------------------------------------

function ConvertFrom-Pcx([byte[]]$data) {
    # Quake II's 8-bit RLE PCX, palette in the last 768 bytes.
    if ($data[0] -ne 0x0A -or $data[2] -ne 1 -or $data[3] -ne 8) {
        throw "not an 8-bit RLE PCX"
    }
    $xmin = [BitConverter]::ToUInt16($data, 4)
    $ymin = [BitConverter]::ToUInt16($data, 6)
    $xmax = [BitConverter]::ToUInt16($data, 8)
    $ymax = [BitConverter]::ToUInt16($data, 10)
    $w = $xmax - $xmin + 1
    $h = $ymax - $ymin + 1
    $stride = [BitConverter]::ToUInt16($data, 66)
    $palOfs = $data.Length - 768

    $pixels = New-Object byte[] ($stride * $h)
    $n = 0
    $i = 128
    while ($n -lt $pixels.Length -and $i -lt $palOfs) {
        $b = $data[$i]; $i++
        if (($b -band 0xC0) -eq 0xC0) {
            $run = $b -band 0x3F
            $v = $data[$i]; $i++
            for ($k = 0; $k -lt $run -and $n -lt $pixels.Length; $k++) {
                $pixels[$n] = $v; $n++
            }
        } else {
            $pixels[$n] = $b; $n++
        }
    }

    $img = New-Img $w $h
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        $out = $y * $w * 4
        for ($x = 0; $x -lt $w; $x++) {
            $v = $pixels[$row + $x]
            $o = $out + $x * 4
            if ($v -eq 255) {
                # Quake II's transparent index in HUD pics.
                $img.P[$o] = 0; $img.P[$o+1] = 0; $img.P[$o+2] = 0; $img.P[$o+3] = 0
            } else {
                $p = $palOfs + $v * 3
                $img.P[$o]   = $data[$p+2]   # B
                $img.P[$o+1] = $data[$p+1]   # G
                $img.P[$o+2] = $data[$p]     # R
                $img.P[$o+3] = 255
            }
        }
    }
    return $img
}

# --- the transformations --------------------------------------------------

function Remove-IconFrame($img, [int]$tolerance = 12) {
    # Drop the icon's frame so it floats on the ring like Team Beef's do. The
    # frame is whatever the outer ring of pixels is made of, and no colour in it
    # also appears in the item's art, so matching against the border colours
    # separates the two. The tolerance was fitted rather than guessed: against
    # every baseq2 icon Team Beef drew it reproduces their alpha to 88.9% at 0
    # and 96.7% at 12, falling away either side.
    $w = $img.W; $h = $img.H
    $border = @{}
    for ($x = 0; $x -lt $w; $x++) {
        foreach ($y in @(0, ($h - 1))) {
            $o = ($y * $w + $x) * 4
            $border["$($img.P[$o]),$($img.P[$o+1]),$($img.P[$o+2])"] = $true
        }
    }
    for ($y = 0; $y -lt $h; $y++) {
        foreach ($x in @(0, ($w - 1))) {
            $o = ($y * $w + $x) * 4
            $border["$($img.P[$o]),$($img.P[$o+1]),$($img.P[$o+2])"] = $true
        }
    }
    # Flatten to a list of triples for the comparison below.
    $tris = New-Object System.Collections.ArrayList
    foreach ($k in $border.Keys) {
        $p = $k.Split(',')
        [void]$tris.Add(@([int]$p[0], [int]$p[1], [int]$p[2]))
    }

    $out = New-Img $w $h
    [Array]::Copy($img.P, $out.P, $img.P.Length)
    for ($i = 0; $i -lt $w * $h; $i++) {
        $o = $i * 4
        $b = $img.P[$o]; $g = $img.P[$o+1]; $r = $img.P[$o+2]
        foreach ($t in $tris) {
            $d = [Math]::Abs($b - $t[0])
            $d2 = [Math]::Abs($g - $t[1]); if ($d2 -gt $d) { $d = $d2 }
            $d2 = [Math]::Abs($r - $t[2]); if ($d2 -gt $d) { $d = $d2 }
            if ($d -le $tolerance) {
                $out.P[$o] = 0; $out.P[$o+1] = 0; $out.P[$o+2] = 0; $out.P[$o+3] = 0
                break
            }
        }
    }
    return $out
}

function New-SelectedVariant($img) {
    # The orange one the cursor lands on. Ratios measured off their art:
    # R 2.02x, G 0.787x, B 0.108x of the icon's luminance.
    $out = New-Img $img.W $img.H
    [Array]::Copy($img.P, $out.P, $img.P.Length)
    for ($i = 0; $i -lt $img.W * $img.H; $i++) {
        $o = $i * 4
        $a = $img.P[$o+3]
        if ($a -eq 0) { continue }
        $b = $img.P[$o]; $g = $img.P[$o+1]; $r = $img.P[$o+2]
        $lum = 0.299 * $r + 0.587 * $g + 0.114 * $b
        $nr = [Math]::Min(255, [int][Math]::Truncate($lum * 2.02))
        $ng = [Math]::Min(255, [int][Math]::Truncate($lum * 0.787))
        $nb = [Math]::Min(255, [int][Math]::Truncate($lum * 0.108))
        $out.P[$o] = $nb; $out.P[$o+1] = $ng; $out.P[$o+2] = $nr; $out.P[$o+3] = $a
    }
    return $out
}

function New-Ring([int]$size = 400, [double]$inner = 0.609, [double]$outer = 0.97) {
    # Team Beef's is a stippled black annulus - a checkerboard rather than a
    # solid fill, so it reads as a ring without hiding what is behind it.
    # Measured off theirs: the band runs 0.609 to 0.97 of the half-width and
    # takes every pixel where (x + y) is even, which matches their own image on
    # 98.4% of pixels.
    $img = New-Img $size $size
    $centre = ($size - 1) / 2.0
    for ($y = 0; $y -lt $size; $y++) {
        $dy = $y - $centre
        for ($x = 0; $x -lt $size; $x++) {
            if ((($x + $y) % 2) -ne 0) { continue }
            $dx = $x - $centre
            $radius = [Math]::Sqrt($dx * $dx + $dy * $dy) / $centre
            if ($radius -ge $inner -and $radius -le $outer) {
                $o = ($y * $size + $x) * 4
                $img.P[$o] = 0; $img.P[$o+1] = 0; $img.P[$o+2] = 0; $img.P[$o+3] = 255
            }
        }
    }
    return $img
}

function New-Cursor([int]$size = 10) {
    # A small X, black with a grey edge, as theirs is.
    $img = New-Img $size $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $d = [Math]::Min([Math]::Abs($x - $y), [Math]::Abs($x + $y - ($size - 1)))
            $o = ($y * $size + $x) * 4
            if ($d -eq 0) {
                $img.P[$o] = 0; $img.P[$o+1] = 0; $img.P[$o+2] = 0; $img.P[$o+3] = 255
            } elseif ($d -eq 1) {
                $img.P[$o] = 5; $img.P[$o+1] = 5; $img.P[$o+2] = 5; $img.P[$o+3] = 255
            } elseif ($d -eq 2) {
                $img.P[$o] = 174; $img.P[$o+1] = 174; $img.P[$o+2] = 174; $img.P[$o+3] = 255
            }
        }
    }
    return $img
}

# --- which icons each game needs ------------------------------------------

# Team Beef named one of their PNGs for the item rather than for the pic it came
# from. Everything else matches the pak name exactly.
$script:WheelAliases = @{ 'w_grenades' = 'w_hgrenade' }

function Get-WantedIcons([string]$manifest) {
    # Read from wheel-icons.txt, which the Python tool regenerates out of the
    # wheel tables in cl_screen.c so the two cannot drift. A release carries the
    # manifest but not the source.
    if (-not (Test-Path $manifest)) { throw "no $manifest to read the icon list from" }
    $wanted = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($manifest)) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $i = $t.IndexOf(':')
        if ($i -lt 0) { continue }
        $pack = $t.Substring(0, $i)
        $name = $t.Substring($i + 1)
        if (-not $wanted.ContainsKey($pack)) { $wanted[$pack] = New-Object System.Collections.ArrayList }
        [void]$wanted[$pack].Add($name)
    }
    return $wanted
}

function Build-WheelArt([string]$quake2Dir, [string]$installDir, [string]$manifest) {
    $wanted = Get-WantedIcons $manifest
    $tbWheel = PathJoin $installDir 'baseq2\wheel'
    $basePak = PathJoin $quake2Dir 'baseq2\pak0.pak'
    $baseIndex = $null
    if (Test-Path $basePak) { $baseIndex = Get-PakIndex $basePak }

    $total = 0
    foreach ($pack in @('baseq2', 'xatrix', 'rogue')) {
        if (-not $wanted.ContainsKey($pack)) { continue }

        # baseq2 is done first, and once it has been its wheel is what the packs
        # resolve to through the search path, so re-read it each time round.
        $have = @{}
        if (Test-Path $tbWheel) {
            foreach ($f in Get-ChildItem $tbWheel -Filter *.png -ErrorAction SilentlyContinue) {
                $have[$f.BaseName] = $true
            }
        }

        $packPak = PathJoin $quake2Dir "$pack\pak0.pak"
        if (-not (Test-Path $packPak)) { continue }

        # The ring and the cursor are not game art and cannot be extracted, so
        # they are drawn - and only when absent, so Team Beef's own are never
        # overwritten.
        if ($pack -eq 'baseq2') {
            [void](New-Item -ItemType Directory -Force $tbWheel)
            foreach ($pair in @(@('ring', 'New-Ring'), @('cursor', 'New-Cursor'))) {
                $target = PathJoin $tbWheel ($pair[0] + '.png')
                if (-not (Test-Path $target)) {
                    Save-Img (& $pair[1]) $target
                    Write-Host ("    drew " + $pair[0] + ".png")
                    $total++
                }
            }
        }

        $index = Get-PakIndex $packPak
        $outDir = PathJoin $installDir "$pack\wheel"
        [void](New-Item -ItemType Directory -Force $outDir)

        $written = 0
        $missing = New-Object System.Collections.ArrayList
        foreach ($name in $wanted[$pack]) {
            $already = $have.ContainsKey($name)
            if ($already -and -not ($pack -eq 'baseq2' -and
                    -not (Test-Path (PathJoin $outDir "$name.png")))) {
                continue
            }

            $pic = $name
            if ($script:WheelAliases.ContainsKey($name)) { $pic = $script:WheelAliases[$name] }

            $data = Read-PakFile $packPak $index "pics/$pic.pcx"
            if ($null -eq $data -and $null -ne $baseIndex) {
                $data = Read-PakFile $basePak $baseIndex "pics/$pic.pcx"
            }
            if ($null -eq $data) { [void]$missing.Add($name); continue }

            $icon = Remove-IconFrame (ConvertFrom-Pcx $data)
            Save-Img $icon (PathJoin $outDir "$name.png")
            Save-Img (New-SelectedVariant $icon) (PathJoin $outDir "${name}_selected.png")
            $written++
            $total += 2
        }

        Write-Host ("    " + $pack + ": " + $written + " icons")
        if ($missing.Count -gt 0) {
            Write-Host ("    not found in the paks: " + ($missing -join ', '))
        }
    }

    Write-Host ("    " + $total + " files written.")
}
