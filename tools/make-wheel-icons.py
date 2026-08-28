#!/usr/bin/env python3
"""
Build the mission packs' weapon wheel icons from the owner's own Quake II data.

Team Beef's wheel art covers baseq2's eleven weapons and six items. The
Reckoning and Ground Zero add nine weapons and three powerups between them, and
nothing ships art for those. Their HUD icons are already in the packs' own paks
though, at exactly the 24x24 the wheel draws - and comparing Team Beef's
w_railgun.png against baseq2's pics/w_railgun.pcx shows theirs *is* the game's
own icon, unchanged. So the same icons, extracted from the paks the owner
already has, are the right source and nothing needs redistributing.

The "_selected" variant is the orange one the wheel shows under the cursor.
Measured off their w_railgun pair, it is a colourise of the icon's luminance:
R 2.02x, G 0.787x, B 0.108x.

The icon list is read out of src/client/cl_screen.c rather than repeated here,
so the two cannot drift apart.

Usage:
    python tools/make-wheel-icons.py <quake2-dir> <install-dir>

<quake2-dir> is the folder holding baseq2/, xatrix/ and rogue/ - a Steam or GOG
Quake II install. <install-dir> is where the port lives. Needs Pillow.
"""

import os
import re
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("This needs Pillow:  pip install pillow")


# --- pak reading ---------------------------------------------------------

def pak_index(path):
    with open(path, "rb") as f:
        magic, ofs, ln = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            raise ValueError(f"{path} is not a pak file")
        f.seek(ofs)
        out = {}
        for _ in range(ln // 64):
            rec = f.read(64)
            name = rec[:56].split(b"\0")[0].decode("latin-1").lower()
            out[name] = struct.unpack("<ii", rec[56:64])
    return out


def pak_read(path, index, name):
    entry = index.get(name.lower())
    if entry is None:
        return None
    ofs, ln = entry
    with open(path, "rb") as f:
        f.seek(ofs)
        return f.read(ln)


# --- pcx decoding --------------------------------------------------------

def pcx_to_rgba(data):
    """Quake II's 8-bit RLE PCX, palette in the last 768 bytes."""
    manuf, _ver, enc, bpp, xmin, ymin, xmax, ymax = struct.unpack("<BBBBHHHH", data[:12])
    if manuf != 0x0A or enc != 1 or bpp != 8:
        raise ValueError("not an 8-bit RLE PCX")
    width = xmax - xmin + 1
    height = ymax - ymin + 1
    stride = struct.unpack("<H", data[66:68])[0]
    palette = data[-768:]

    pixels = bytearray()
    i = 128
    end = len(data) - 768
    while len(pixels) < stride * height and i < end:
        byte = data[i]
        i += 1
        if (byte & 0xC0) == 0xC0:
            run = byte & 0x3F
            pixels.extend([data[i]] * run)
            i += 1
        else:
            pixels.append(byte)

    image = Image.new("RGBA", (width, height))
    put = image.load()
    for y in range(height):
        row = pixels[y * stride:(y + 1) * stride]
        for x in range(width):
            v = row[x]
            if v == 255:      # Quake II's transparent index in HUD pics
                put[x, y] = (0, 0, 0, 0)
            else:
                put[x, y] = (palette[v * 3], palette[v * 3 + 1], palette[v * 3 + 2], 255)
    return image


def cut_background(image, tolerance=12):
    """Drop the icon's frame, so it floats on the ring like Team Beef's do.

    Their art is transparent everywhere the HUD icon's background frame was -
    251 opaque pixels of 576 on w_railgun. The frame is whatever the outer ring
    of pixels is made of, and no colour in it also appears in the item's art, so
    matching against the border colours separates the two.

    The tolerance was fitted, not guessed: run against every baseq2 icon Team
    Beef drew, this rule reproduces their own alpha to 88.9% at tolerance 0,
    96.7% at 12, and falls away either side. 12 it is.
    """
    border = set()
    px = image.load()
    for x in range(image.width):
        border.add(px[x, 0][:3])
        border.add(px[x, image.height - 1][:3])
    for y in range(image.height):
        border.add(px[0, y][:3])
        border.add(px[image.width - 1, y][:3])

    out = image.copy()
    put = out.load()
    for y in range(out.height):
        for x in range(out.width):
            colour = put[x, y][:3]
            if any(max(abs(colour[i] - other[i]) for i in range(3)) <= tolerance
                    for other in border):
                put[x, y] = (0, 0, 0, 0)
    return out


def selected_variant(image):
    """The orange one the cursor lands on. Ratios measured off their art."""
    out = image.copy()
    put = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = put[x, y]
            if not a:
                continue
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            put[x, y] = (min(255, int(lum * 2.02)),
                         min(255, int(lum * 0.787)),
                         min(255, int(lum * 0.108)),
                         a)
    return out


# --- which icons each pack needs -----------------------------------------

# Team Beef named one of their PNGs for the item rather than for the pic it came
# from. Everything else matches the pak name exactly.
ALIASES = {
    "w_grenades": "w_hgrenade",
}

TABLES = {
    "xatrix": ("xatrixWeaponIcons", "xatrixItemIcons"),
    "rogue": ("rogueWeaponIcons", "rogueItemIcons"),
}


def icons_wanted(source_file):
    """Read the icon and ammo names straight out of the wheel tables."""
    text = open(source_file, encoding="latin-1").read()
    wanted = {}
    for pack, names in TABLES.items():
        need = set()
        for table in names:
            m = re.search(r"\b" + table + r"\[\d+\]\s*=\s*\{(.*?)\n\};", text, re.S)
            if not m:
                raise SystemExit(f"could not find {table} in {source_file}")
            for row in re.finditer(
                    r'\{"([^"]+)",\s*\d+,\s*"[^"]*",\s*(NULL|"[^"]*")', m.group(1)):
                need.add(row.group(1))
                ammo = row.group(2)
                if ammo != "NULL":
                    need.add("a_" + ammo.strip('"'))
        wanted[pack] = sorted(need)
    return wanted


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())

    quake2_dir, install_dir = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    wanted = icons_wanted(os.path.join(here, "src", "client", "cl_screen.c"))

    # Anything Team Beef already drew is used as it is, through the search path.
    have = set()
    tb_wheel = os.path.join(install_dir, "baseq2", "wheel")
    if os.path.isdir(tb_wheel):
        have = {f[:-4] for f in os.listdir(tb_wheel) if f.endswith(".png")}

    base_pak = os.path.join(quake2_dir, "baseq2", "pak0.pak")
    base_index = pak_index(base_pak) if os.path.isfile(base_pak) else {}

    total = 0
    for pack, names in wanted.items():
        pack_pak = os.path.join(quake2_dir, pack, "pak0.pak")
        if not os.path.isfile(pack_pak):
            print(f"{pack}: no pak0.pak in {quake2_dir}/{pack} - skipped")
            continue

        index = pak_index(pack_pak)
        out_dir = os.path.join(install_dir, pack, "wheel")
        os.makedirs(out_dir, exist_ok=True)

        written, missing = [], []
        for name in names:
            if name in have:
                continue    # Team Beef's own art covers it

            pic = ALIASES.get(name, name)
            data = pak_read(pack_pak, index, f"pics/{pic}.pcx")
            if data is None and base_index:
                data = pak_read(base_pak, base_index, f"pics/{pic}.pcx")
            if data is None:
                missing.append(name)
                continue

            icon = cut_background(pcx_to_rgba(data))
            icon.save(os.path.join(out_dir, f"{name}.png"))
            selected_variant(icon).save(os.path.join(out_dir, f"{name}_selected.png"))
            written.append(name)
            total += 2

        print(f"{pack}: wrote {len(written)} icons to {out_dir}")
        for name in written:
            print(f"    {name}")
        if missing:
            print(f"  NOT FOUND in the paks: {', '.join(missing)}")

    print(f"{total} files written.")


if __name__ == "__main__":
    main()
