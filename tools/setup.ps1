# Prepare an install to play: game data, the expansions, music and wheel art.
#
# A port of setup.py that needs nothing installed. PowerShell ships with Windows
# and System.Drawing comes with it, so a downloaded release runs this and plays -
# no Python, no Pillow, no download. setup.py is kept for building releases here,
# and the two are verified against each other: every generated file compared, and
# all 80 wheel images compared pixel by pixel.
#
# Everything it produces is built from the Quake II data already on this machine.
# No game data is carried in the release, and nothing produced here may be
# redistributed.
#
#   powershell -ExecutionPolicy Bypass -File tools\setup.ps1 [install dir] [quake2 dir]

param(
    [string]$InstallDir = ".",
    [string]$Quake2Dir = "",
    # Whether to fetch Team Beef's HD assets from their own GitHub release when
    # they are not already on this machine. On by default and deliberately not a
    # question: the release says this is what Setup does, so asking again only
    # gives somebody a chance to answer wrongly and end up with a worse install.
    [ValidateSet('yes', 'no')]
    [string]$Extras = 'yes'
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. ([System.IO.Path]::Combine($here, 'wheel-art.ps1'))

$Quake2Guesses = @(
    "C:\Program Files (x86)\Steam\steamapps\common\Quake 2",
    "C:\Program Files\Steam\steamapps\common\Quake 2",
    "D:\SteamLibrary\steamapps\common\Quake 2",
    "E:\SteamLibrary\steamapps\common\Quake 2",
    "C:\GOG Games\Quake 2",
    "C:\Program Files (x86)\GOG Galaxy\Games\Quake 2",
    "C:\Program Files (x86)\Quake II"
)

# The Prox Launcher is a reskinned Grenade Launcher, so Team Beef's tuned value
# for the launcher is right for it and does not need an eye. Measured, not
# assumed: models/weapons/v_plaunch and v_launch have the same 208 vertices, 384
# triangles and 66 frames, the same frame names, and byte-identical vertex data
# in all 66 of them.
$ProxOffset = "10.0,5.0,-8.0,-3.0,0.0,0.0"   # = WEAP_GRENADELAUNCHER
$DefaultOffset = "10.0,7.0,-8.0,-3.0,0.0,0.0"

# Team Beef's, tuned against their HD viewmodels. Both expansions keep Quake
# II's WEAP_ numbering for the weapons they share, so these are right there too.
$BaseOffsets = @(
    @(1,  "WEAP_BLASTER",         "17.0,4.5,-8.0,0.0,2.0,0.0"),
    @(2,  "WEAP_SHOTGUN",         "12.0,7.4,-8.0,-6.0,-0.5,0.0"),
    @(3,  "WEAP_SUPERSHOTGUN",    "10.0,6.5,-8.0,-3.0,0.0,0.0"),
    @(4,  "WEAP_MACHINEGUN",      "17.0,7.0,-8.0,-3.0,0.0,0.0"),
    @(5,  "WEAP_CHAINGUN",        "-6.0,3.4,-8.0,-1.5,-0.8,0.0"),
    @(6,  "WEAP_GRENADES",        "13.0,0.0,-7.0,0.0,0.0,0.0"),
    @(7,  "WEAP_GRENADELAUNCHER", "10.0,5.0,-8.0,-3.0,0.0,0.0"),
    @(8,  "WEAP_ROCKETLAUNCHER",  "10.0,3.6,-8.0,-3.0,0.0,0.0"),
    @(9,  "WEAP_HYPERBLASTER",    "10.0,5.0,-8.0,-1.5,0.0,0.0"),
    @(10, "WEAP_RAILGUN",         "10.0,6.0,-8.0,-3.0,0.0,0.0"),
    @(11, "WEAP_BFG",             "10.0,7.0,-8.0,-3.0,0.0,0.0")
)

# gamedir, name, and the WEAP_ numbers this game adds - each with the offset to
# write, or $null where only an eye in a headset can settle it.
$Expansions = @(
    @{ Dir = 'xatrix'; Title = 'The Reckoning'; Extra = @(
        @(12, "WEAP_PHALANX", $null), @(13, "WEAP_BOOMER - the Ionripper", $null)) },
    @{ Dir = 'rogue'; Title = 'Ground Zero'; Extra = @(
        @(12, "WEAP_DISRUPTOR", $null), @(13, "WEAP_ETFRIFLE", $null),
        @(14, "WEAP_PLASMA - the Plasma Beam", $null),
        @(15, "WEAP_PROXLAUNCH", $ProxOffset), @(16, "WEAP_CHAINFIST", $null)) }
)

# Renderer settings from Team Beef's own config, which are theirs rather than
# yquake2's defaults. Written into config.cfg rather than autoexec.cfg so that
# changing them in the menus afterwards sticks.
$TeamBeefConfig = @(
    @("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR"),
    @("gl_anisotropic", "16"),
    @("gl_shadows", "1"),
    @("gl1_stencilshadow", "1")
)

$ExtraFiles = @("pak6.pak", "pak99.pak", "vignette.tga")

# Team Beef publish their Quest build on their own GitHub, and the APK is a zip
# with these three inside it. Fetching from there rather than carrying them here
# keeps the download off our release entirely - we host nothing, so the archive
# stays 6 MB and there is no question about what is in it.
#
# The sizes and hashes are what the v1.0.9 APK holds, checked against the copies
# the port was built and tuned against. A mismatch means the release moved and
# is a thing to look at rather than to install.
$ExtrasUrl = 'https://github.com/DrBeef/Quake2Quest/releases/download/v1.0.9/quake2quest-v1.0.9.apk'
$ExtrasInApk = @{
    'pak6.pak'     = @{ Entry = 'assets/pak6.pak';     Size = 147653659; Sha = '7B29585E98EA522E7354E444E9A6D94527B736BB700465FCE1C88B01BE0548B8' }
    'pak99.pak'    = @{ Entry = 'assets/pak99.pak';    Size = 38862521;  Sha = 'B9DCF90E6A17A4BA11A975339DAA14B1BC9AD44470D73D7EA995B007C7D51B2F' }
    'vignette.tga' = @{ Entry = 'assets/vignette.tga'; Size = 801517;    Sha = 'D2AB24B4451EBA2F16B49FBEF8702D390526319971BD749DB8DC89567211BCC4' }
}

function Write-TextCrLf([string]$path, [string[]]$lines) {
    # ASCII with CRLF, matching what setup.py writes byte for byte. ASCII has no
    # BOM, which a config parser would choke on.
    $text = ($lines -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText($path, $text, [System.Text.Encoding]::ASCII)
}

function Test-Quake2Dir([string]$path) {
    if (-not $path) { return $false }
    try { return (Test-Path -PathType Leaf (PathJoin $path 'baseq2\pak0.pak')) }
    catch { return $false }
}

function Get-SteamLibraries {
    # Ask Steam where its libraries are rather than guessing drive letters. A
    # hardcoded list only ever finds the machine it was written on: it worked
    # here and found nothing on the first PC it was tried on, which had Steam in
    # a library the list did not name.
    $roots = New-Object System.Collections.ArrayList

    foreach ($k in @('HKCU:\Software\Valve\Steam',
                     'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
                     'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty $k -ErrorAction SilentlyContinue
        } catch { continue }
        if (-not $v) { continue }
        foreach ($p in @($v.SteamPath, $v.InstallPath)) {
            # SteamPath comes back lowercase with forward slashes.
            if ($p) { [void]$roots.Add(($p -replace '/', '\')) }
        }
    }

    $libs = New-Object System.Collections.ArrayList
    foreach ($root in $roots) {
        if (-not (Test-Path -PathType Container $root)) { continue }
        if ($libs -notcontains $root) { [void]$libs.Add($root) }

        # Every library folder, including ones on other drives, is listed here.
        $vdf = PathJoin $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -PathType Leaf $vdf)) { continue }
        foreach ($line in [System.IO.File]::ReadAllLines($vdf)) {
            $m = [regex]::Match($line, '"path"\s+"(.+?)"')
            if (-not $m.Success) { continue }
            $lib = $m.Groups[1].Value -replace '\\\\', '\'
            if ($libs -notcontains $lib) { [void]$libs.Add($lib) }
        }
    }
    return $libs
}

function Get-Quake2Score([string]$path) {
    # How complete an install is, so the best one wins rather than the first one
    # found. Quake II RTX is the case that forced this: it is a different product
    # that ships demo content, and it has a baseq2\pak0.pak like everything else,
    # so a search that stops at the first hit can pick it over the real game and
    # then report that the expansions are not installed.
    $p0 = PathJoin $path 'baseq2\pak0.pak'
    if (-not (Test-Path -PathType Leaf $p0)) { return -1 }

    $score = 0
    # The retail and remaster pak0 are about 184 MB. The demo and RTX ones are a
    # fraction of that, which is the difference between the whole game and a
    # sample of it.
    if ((Get-Item $p0).Length -gt 100MB) { $score += 8 }
    if (Test-Path -PathType Leaf (PathJoin $path 'xatrix\pak0.pak')) { $score += 4 }
    if (Test-Path -PathType Leaf (PathJoin $path 'rogue\pak0.pak')) { $score += 4 }
    if (Test-Path -PathType Container (PathJoin $path 'rerelease\baseq2\music')) { $score += 2 }
    return $score
}

function Find-Quake2([string]$given) {
    if ($given) {
        if (Test-Path -PathType Container $given) { return $given } else { return $null }
    }
    if ($env:Q2VR_QUAKEDIR -and (Test-Path -PathType Container $env:Q2VR_QUAKEDIR)) {
        return $env:Q2VR_QUAKEDIR
    }

    # Gather every candidate rather than stopping at the first, then take the
    # most complete one.
    $cands = New-Object System.Collections.ArrayList

    foreach ($lib in (Get-SteamLibraries)) {
        $common = PathJoin $lib 'steamapps\common'
        if (-not (Test-Path -PathType Container $common)) { continue }
        try { $dirs = Get-ChildItem -LiteralPath $common -Directory -ErrorAction SilentlyContinue }
        catch { continue }
        foreach ($d in $dirs) { [void]$cands.Add($d.FullName) }
    }

    # GOG records each game's folder under its own key.
    foreach ($k in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games',
                     'HKLM:\SOFTWARE\GOG.com\Games')) {
        try { $keys = Get-ChildItem $k -ErrorAction SilentlyContinue } catch { continue }
        foreach ($g in $keys) {
            try { $v = Get-ItemProperty $g.PSPath -ErrorAction SilentlyContinue } catch { continue }
            if ($v -and $v.path) { [void]$cands.Add($v.path) }
        }
    }

    # And the fixed guesses, which cover a hand-copied install.
    foreach ($p in $Quake2Guesses) { [void]$cands.Add($p) }

    $best = $null
    $bestScore = -1
    $seen = @{}
    $script:Quake2Rejected = New-Object System.Collections.ArrayList
    $script:Quake2Extra = New-Object System.Collections.ArrayList

    foreach ($c in $cands) {
        $key = $c.ToLower().TrimEnd('\')
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $sc = Get-Quake2Score $c
        if ($sc -lt 0) {
            # No baseq2, but it may still be an expansion on its own.
            if ((Test-Path -PathType Leaf (PathJoin $c 'xatrix\pak0.pak')) -or
                (Test-Path -PathType Leaf (PathJoin $c 'rogue\pak0.pak'))) {
                [void]$script:Quake2Extra.Add($c)
            }
            continue
        }
        [void]$script:Quake2Extra.Add($c)
        if ($sc -gt $bestScore) {
            if ($best) { [void]$script:Quake2Rejected.Add($best) }
            $best = $c
            $bestScore = $sc
        } else {
            [void]$script:Quake2Rejected.Add($c)
        }
    }

    $script:Quake2Score = $bestScore
    return $best
}

function Copy-IfNeeded([string]$src, [string]$dst, [string]$label) {
    # Copy unless it is already there. Setup gets run more than once.
    if (-not (Test-Path -PathType Leaf $src)) { return $false }
    if (Test-Path -PathType Leaf $dst) {
        if ((Get-Item $dst).Length -eq (Get-Item $src).Length) { return $true }
    }
    if ($label) { Write-Host ("    " + $label) }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    return $true
}

function Copy-TreeFlat([string]$src, [string]$dst) {
    if (-not (Test-Path -PathType Container $src)) { return $false }
    [void](New-Item -ItemType Directory -Force $dst)
    foreach ($f in Get-ChildItem -LiteralPath $src -File) {
        [void](Copy-IfNeeded $f.FullName (PathJoin $dst $f.Name) $null)
    }
    return $true
}

function Write-Autoexec([string]$path, [string]$title, $extra) {
    # The per-weapon offsets, which the engine reads at startup. Each gamedir
    # needs its own, complete: the engine execs autoexec.cfg once, taking the
    # first match on the search path, so an expansion without one runs with none.
    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add("// Weapon offsets. Team Beef's, tuned for their HD viewmodels.")
    [void]$lines.Add("// Values are:  backwards, left, up, pitch (down), yaw, roll")
    [void]$lines.Add("//")
    [void]$lines.Add("// Written by Setup, and rewritten every time it runs.")
    [void]$lines.Add("")

    foreach ($o in $BaseOffsets) {
        [void]$lines.Add("//" + $o[1])
        [void]$lines.Add('set vr_weapon_adjustment_' + $o[0] + ' "' + $o[2] + '"')
    }

    if ($extra) {
        [void]$lines.Add("")
        [void]$lines.Add("// $title's own weapons. Team Beef never shipped this game, so")
        [void]$lines.Add("// these start at the engine's default and want tuning by eye in")
        [void]$lines.Add("// the headset - except where the model is one Quake II already")
        [void]$lines.Add("// has, and their tuned value for it carries over.")
        foreach ($e in $extra) {
            $v = $e[2]; if (-not $v) { $v = $DefaultOffset }
            [void]$lines.Add("//" + $e[1])
            [void]$lines.Add('set vr_weapon_adjustment_' + $e[0] + ' "' + $v + '"')
        }
    }

    [void]$lines.Add("")
    [void]$lines.Add("// Anything tuned in the headset is written to weapons.cfg by")
    [void]$lines.Add("// 'vrweapon save'. It is exec'd last so it wins, and Setup never")
    [void]$lines.Add("// writes over it - this file is rewritten every run, that one is not.")
    [void]$lines.Add("exec weapons.cfg")

    Write-TextCrLf $path $lines.ToArray()
}

function Write-WeaponsStub([string]$path) {
    # Create it if absent and never touch it again: this is where headset tuning
    # lands, and Setup rewriting autoexec.cfg every run must not destroy it.
    if (Test-Path $path) { return }
    Write-TextCrLf $path @(
        "// Weapon offsets tuned in the headset.",
        "//",
        "// Turn 'weapon alignment' on in PC Options, adjust with the off",
        "// hand's stick, then type 'vrweapon save' at the console. This",
        "// file is rewritten by that and by nothing else - Setup leaves it",
        "// alone, so re-running Setup cannot undo any of it.",
        "")
}

function Write-DefaultConfig([string]$path) {
    # Only when there is none. The engine rewrites config.cfg every time it
    # exits, so on the second run this file is the player's, not Setup's.
    if (Test-Path -PathType Leaf $path) { return }
    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add("// Written by Setup, once, because this gamedir had no config.")
    [void]$lines.Add("// These are Team Beef's renderer settings rather than the engine's")
    [void]$lines.Add("// defaults. Change them in the menus - the engine rewrites this file")
    [void]$lines.Add("// when it exits and Setup will not touch it again.")
    [void]$lines.Add("")
    foreach ($c in $TeamBeefConfig) {
        [void]$lines.Add('set ' + $c[0] + ' "' + $c[1] + '"')
    }
    Write-TextCrLf $path $lines.ToArray()
}

function Get-ExtrasFromTeamBeef([string]$baseDst, $missing) {
    # Their APK, from their own release page, unpacked on this machine. Nothing
    # is redistributed by us and nothing is kept afterwards but the three files.
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    # PowerShell 5.1 still defaults to TLS 1.0, which GitHub refuses.
    try {
        [Net.ServicePointManager]::SecurityProtocol =
            [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    } catch { }

    $tmp = [System.IO.Path]::Combine($env:TEMP, 'q2vr-teambeef.apk')
    Write-Host ""
    Write-Host "  Downloading Team Beef's assets from their GitHub release."
    Write-Host "  About 169 MB. This happens once."
    Write-Host ("  " + $ExtrasUrl)

    try {
        $wc = New-Object System.Net.WebClient
        $wc.Headers.Add('User-Agent', 'Quake2VR-PCVR-Setup')
        $wc.DownloadFile($ExtrasUrl, $tmp)
        $wc.Dispose()
    } catch {
        Write-Host ("  download failed: " + $_.Exception.Message)
        Write-Host "  The game plays without these - retail artwork instead."
        if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
        return @()
    }

    $got = New-Object System.Collections.ArrayList
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($tmp)
        try {
            foreach ($name in $missing) {
                $spec = $ExtrasInApk[$name]
                if (-not $spec) { continue }
                $e = $zip.Entries | Where-Object { $_.FullName -eq $spec.Entry } | Select-Object -First 1
                if (-not $e) {
                    Write-Host ("    " + $name + ": not in the APK - it may have moved")
                    continue
                }
                $out = [System.IO.Path]::Combine($baseDst, $name)
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $out, $true)

                $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $out).Hash
                if ($h -ne $spec.Sha) {
                    Write-Host ("    " + $name + ": does not match what was expected - discarded")
                    Remove-Item $out -Force -ErrorAction SilentlyContinue
                    continue
                }
                Write-Host ("    " + $name + " ok")
                [void]$got.Add($name)
            }
        } finally { $zip.Dispose() }
    } catch {
        Write-Host ("  could not read the APK: " + $_.Exception.Message)
    }

    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    return $got
}

function Write-Launcher([string]$path, [string]$extraArgs) {
    # CRLF, no trailing newline beyond the one, matching setup.py exactly.
    $text = "@echo off`r`nstart `"`" `"%~dp0yquake2.exe`" -portable$extraArgs`r`n"
    [System.IO.File]::WriteAllText($path, $text, [System.Text.Encoding]::ASCII)
}

# --- the install ----------------------------------------------------------

$dest = (Resolve-Path $InstallDir).Path
$quake2 = Find-Quake2 $Quake2Dir

if (-not $quake2) {
    Write-Host "Could not find Quake II."
    Write-Host ""
    $libs = @(Get-SteamLibraries)
    if ($libs.Count -gt 0) {
        Write-Host "Steam libraries searched:"
        foreach ($l in $libs) { Write-Host ("  " + $l) }
    } else {
        Write-Host "No Steam installation was found in the registry."
    }
    Write-Host ""
    Write-Host "If Quake II is somewhere else, point this at the folder that"
    Write-Host "holds baseq2 and run it again - for example:"
    Write-Host ""
    Write-Host "  set Q2VR_QUAKEDIR=D:\Games\Quake 2"
    Write-Host "  Setup.bat"
    Write-Host ""
    Write-Host "The folder wanted is the one containing baseq2\pak0.pak."
    exit 1
}

Write-Host ("Quake II found at " + $quake2)
if ($script:Quake2Rejected -and $script:Quake2Rejected.Count -gt 0) {
    # More than one thing on this machine looks like Quake II - say which was
    # passed over, because picking the wrong one is silent otherwise and shows up
    # much later as missing expansions.
    Write-Host "  also seen, and less complete:"
    foreach ($r in $script:Quake2Rejected) { Write-Host ("    " + $r) }
}
Write-Host ("Installing into  " + $dest)
Write-Host ""

# Quake II itself.
$baseSrc = PathJoin $quake2 'baseq2'
$baseDst = PathJoin $dest 'baseq2'
[void](New-Item -ItemType Directory -Force (PathJoin $baseDst 'save'))
Write-Host "Quake II"

if (-not (Copy-IfNeeded (PathJoin $baseSrc 'pak0.pak') (PathJoin $baseDst 'pak0.pak') 'pak0.pak')) {
    Write-Host ("  no baseq2/pak0.pak under " + $quake2)
    exit 1
}
foreach ($n in @('pak1.pak', 'pak2.pak', 'maps.lst')) {
    [void](Copy-IfNeeded (PathJoin $baseSrc $n) (PathJoin $baseDst $n) $n)
}
foreach ($sub in @('video', 'players')) {
    [void](Copy-TreeFlat (PathJoin $baseSrc $sub) (PathJoin $baseDst $sub))
}

# The soundtrack. Retail Quake II played it off the CD and no download has it;
# the 2023 remaster ships the same tracks and comes with the Steam release.
$musicSrc = PathJoin $quake2 'rerelease\baseq2\music'
$musicDst = PathJoin $baseDst 'music'
if (Test-Path -PathType Container $musicSrc) {
    [void](Copy-TreeFlat $musicSrc $musicDst)
    $n = (Get-ChildItem $musicDst -File).Count
    Write-Host ("  soundtrack: " + $n + " tracks")
} elseif (-not (Test-Path -PathType Container $musicDst)) {
    Write-Host "  no soundtrack found - the remaster's music folder is the source,"
    Write-Host "  and it is not in this install. The game plays fine without it."
}

# The expansions.
$installed = New-Object System.Collections.ArrayList
foreach ($x in $Expansions) {
    # The chosen install first, then any other folder that held game data. Steam
    # has sold the mission packs both inside Quake II and as their own products,
    # and in the second case they are in a different folder entirely.
    $roots = New-Object System.Collections.ArrayList
    [void]$roots.Add($quake2)
    if ($script:Quake2Extra) {
        foreach ($e in $script:Quake2Extra) { if ($e -ne $quake2) { [void]$roots.Add($e) } }
    }

    $pak = $null
    $from = $null
    foreach ($r in $roots) {
        $try = PathJoin $r ($x.Dir + '\pak0.pak')
        if (Test-Path -PathType Leaf $try) { $pak = $try; $from = $r; break }
    }
    if (-not $pak) { continue }

    Write-Host $x.Title
    if ($from -ne $quake2) { Write-Host ("    from " + $from) }
    $target = PathJoin $dest $x.Dir
    [void](New-Item -ItemType Directory -Force (PathJoin $target 'save'))
    [void](Copy-IfNeeded $pak (PathJoin $target 'pak0.pak') 'pak0.pak')
    [void](Copy-TreeFlat (PathJoin $from ($x.Dir + '\video')) (PathJoin $target 'video'))
    Write-Autoexec (PathJoin $target 'autoexec.cfg') $x.Title $x.Extra
    Write-WeaponsStub (PathJoin $target 'weapons.cfg')
    Write-DefaultConfig (PathJoin $target 'config.cfg')
    Write-Launcher (PathJoin $dest ('Play ' + $x.Title + ' VR.bat')) (' +set game ' + $x.Dir)
    [void]$installed.Add($x.Title)
}

Write-Autoexec (PathJoin $baseDst 'autoexec.cfg') 'Quake II' $null
Write-WeaponsStub (PathJoin $baseDst 'weapons.cfg')
Write-DefaultConfig (PathJoin $baseDst 'config.cfg')
Write-Launcher (PathJoin $dest 'Play Quake II VR.bat') ''

# Team Beef's own assets, if the owner has their standalone. None of it is
# redistributable and none of it is in a Quake II install, so it is picked up
# only if it is already here or Q2VR_TBDIR points at it.
$extrasSrc = $env:Q2VR_TBDIR
if (-not $extrasSrc) { $extrasSrc = PathJoin $dest 'extras' }
$found = New-Object System.Collections.ArrayList
foreach ($name in $ExtraFiles) {
    if (Test-Path -PathType Leaf (PathJoin $baseDst $name)) {
        [void]$found.Add($name)
    } elseif (Copy-IfNeeded (PathJoin $extrasSrc $name) (PathJoin $baseDst $name) $name) {
        [void]$found.Add($name)
    }
}

# Only what is still missing, and only after looking locally - a copy already on
# this machine always beats a download.
$stillMissing = @($ExtraFiles | Where-Object { $found -notcontains $_ })
if ($stillMissing.Count -gt 0 -and $Extras -ne 'no') {
    foreach ($n in (Get-ExtrasFromTeamBeef $baseDst $stillMissing)) { [void]$found.Add($n) }
}
if ($found -contains 'pak6.pak') {
    $auto = PathJoin $baseDst 'autoexec.cfg'
    $add = "`r`n// pak6 is inert without this.`r`nset gl_retexturing `"1`"`r`n"
    [System.IO.File]::AppendAllText($auto, $add, [System.Text.Encoding]::ASCII)
}

Write-Host "Menu artwork"
try {
    Build-WheelArt $quake2 $dest (PathJoin $here 'wheel-icons.txt')
} catch {
    Write-Host ("  could not build the weapon wheel icons: " + $_.Exception.Message)
    Write-Host "  The game still runs; the wheel will have blank slices."
}

Write-Host ""
Write-Host "Ready."
if ($installed.Count -gt 0) {
    Write-Host ("  Quake II" + "  and " + ($installed -join " and "))
} else {
    Write-Host "  Quake II"
    Write-Host "  No expansions found. If you own The Reckoning or Ground Zero,"
    Write-Host "  install them from Steam and run this again."
}

$missing = @($ExtraFiles | Where-Object { $found -notcontains $_ })
if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host ("  Team Beef's extras are not here: " + ($missing -join ', '))
    Write-Host "  Those are the HD textures, the HD weapon models and the comfort"
    Write-Host "  mask from their Quest standalone. They cannot be included and are"
    Write-Host "  not in a Quake II install. Without them the game plays the same,"
    Write-Host "  with retail artwork. If you have their standalone's data, put"
    Write-Host "  those files in an 'extras' folder here and run this again."
}

Write-Host ""
Write-Host "  Start Virtual Desktop and connect it, then run a Play ... .bat"
exit 0
