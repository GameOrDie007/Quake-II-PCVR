#!/usr/bin/env python3
"""
Prepare an install to play: game data, the expansions, music and menu artwork.

This is what Setup.bat runs in a downloaded release, and what
package-release.sh calls when building one here, so there is one implementation
of it.

Everything it produces is built from the Quake II data already on this machine.
No game data is carried in the release, and nothing produced here may be
redistributed.

Usage: setup.py [install dir] [quake2 dir]

  install dir  defaults to the directory this is run from
  quake2 dir   defaults to $Q2VR_QUAKEDIR, then the usual Steam and GOG paths
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

QUAKE2_GUESSES = [
    "C:/Program Files (x86)/Steam/steamapps/common/Quake 2",
    "C:/Program Files/Steam/steamapps/common/Quake 2",
    "D:/SteamLibrary/steamapps/common/Quake 2",
    "E:/SteamLibrary/steamapps/common/Quake 2",
    "C:/GOG Games/Quake 2",
    "C:/Program Files (x86)/GOG Galaxy/Games/Quake 2",
    "C:/Program Files (x86)/Quake II",
]

# The Prox Launcher is a reskinned Grenade Launcher, so Team Beef's tuned value
# for the launcher is right for it and does not need an eye. Measured, not
# assumed: models/weapons/v_plaunch and v_launch have the same 208 vertices, 384
# triangles and 66 frames, the same frame names, and byte-identical vertex data
# in all 66 of them. The same model in the hand wants the same offset.
PROX_OFFSET = "10.0,5.0,-8.0,-3.0,0.0,0.0"   # = WEAP_GRENADELAUNCHER

# gamedir, name, and the WEAP_ numbers this game adds - each with the offset to
# write for it, or None where only an eye in a headset can settle it
EXPANSIONS = [
    ("xatrix", "The Reckoning",
     [(12, "WEAP_PHALANX", None), (13, "WEAP_BOOMER - the Ionripper", None)]),
    ("rogue", "Ground Zero",
     [(12, "WEAP_DISRUPTOR", None), (13, "WEAP_ETFRIFLE", None),
      (14, "WEAP_PLASMA - the Plasma Beam", None),
      (15, "WEAP_PROXLAUNCH", PROX_OFFSET), (16, "WEAP_CHAINFIST", None)]),
]

# Team Beef's, tuned against their HD viewmodels. Both expansions keep Quake
# II's WEAP_ numbering for the weapons they share, so these are right there too.
BASE_OFFSETS = [
    (1, "WEAP_BLASTER", "17.0,4.5,-8.0,0.0,2.0,0.0"),
    (2, "WEAP_SHOTGUN", "12.0,7.4,-8.0,-6.0,-0.5,0.0"),
    (3, "WEAP_SUPERSHOTGUN", "10.0,6.5,-8.0,-3.0,0.0,0.0"),
    (4, "WEAP_MACHINEGUN", "17.0,7.0,-8.0,-3.0,0.0,0.0"),
    (5, "WEAP_CHAINGUN", "-6.0,3.4,-8.0,-1.5,-0.8,0.0"),
    (6, "WEAP_GRENADES", "13.0,0.0,-7.0,0.0,0.0,0.0"),
    (7, "WEAP_GRENADELAUNCHER", "10.0,5.0,-8.0,-3.0,0.0,0.0"),
    (8, "WEAP_ROCKETLAUNCHER", "10.0,3.6,-8.0,-3.0,0.0,0.0"),
    (9, "WEAP_HYPERBLASTER", "10.0,5.0,-8.0,-1.5,0.0,0.0"),
    (10, "WEAP_RAILGUN", "10.0,6.0,-8.0,-3.0,0.0,0.0"),
    (11, "WEAP_BFG", "10.0,7.0,-8.0,-3.0,0.0,0.0"),
]

DEFAULT_OFFSET = "10.0,7.0,-8.0,-3.0,0.0,0.0"

# Renderer settings from Team Beef's own config, which are theirs rather than
# yquake2's defaults. Written once, into config.cfg rather than autoexec.cfg,
# so that changing them in the menus afterwards sticks - the engine rewrites
# config.cfg on exit, and an autoexec would impose these again every launch.
# gl1_stereo, r_mode and the eye dimensions are not here because the VR layer
# sets them in code.
TEAMBEEF_CONFIG = [
    ("gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR"),
    ("gl_anisotropic", "16"),
    ("gl_shadows", "1"),
    ("gl1_stencilshadow", "1"),
]

EXTRAS = ("pak6.pak", "pak99.pak", "vignette.tga")

LAUNCHER = '@echo off\r\nstart "" "%~dp0yquake2.exe" -portable{args}\r\n'


def find_quake2(given):
    if given:
        return given if os.path.isdir(given) else None

    env = os.environ.get("Q2VR_QUAKEDIR")
    if env and os.path.isdir(env):
        return env

    for path in QUAKE2_GUESSES:
        if os.path.isfile(os.path.join(path, "baseq2", "pak0.pak")):
            return path

    return None


def copy_file(src, dst, label=None):
    """Copy unless it is already there. Setup gets run more than once."""
    if not os.path.isfile(src):
        return False

    if os.path.isfile(dst) and os.path.getsize(dst) == os.path.getsize(src):
        return True

    if label:
        print("    " + label)

    shutil.copyfile(src, dst)
    return True


def copy_tree(src, dst):
    if not os.path.isdir(src):
        return False

    os.makedirs(dst, exist_ok=True)

    for name in os.listdir(src):
        source = os.path.join(src, name)

        if os.path.isfile(source):
            copy_file(source, os.path.join(dst, name))

    return True


def write_autoexec(path, title, extra):
    """The per-weapon offsets, which the engine reads at startup.

    Each gamedir needs its own, complete. The engine execs autoexec.cfg once,
    taking the first match on the search path, so an expansion without one would
    not fall back to baseq2's - it would simply run with none.
    """
    lines = [
        "// Weapon offsets. Team Beef's, tuned for their HD viewmodels.",
        "// Values are:  backwards, left, up, pitch (down), yaw, roll",
        "//",
        "// Written by Setup, and rewritten every time it runs.",
        "",
    ]

    for number, name, value in BASE_OFFSETS:
        lines.append("//" + name)
        lines.append('set vr_weapon_adjustment_%d "%s"' % (number, value))

    if extra:
        lines += [
            "",
            "// %s's own weapons. Team Beef never shipped this game, so" % title,
            "// these start at the engine's default and want tuning by eye in",
            "// the headset - except where the model is one Quake II already",
            "// has, and their tuned value for it carries over.",
        ]

        for number, name, value in extra:
            lines.append("//" + name)
            lines.append('set vr_weapon_adjustment_%d "%s"'
                         % (number, value or DEFAULT_OFFSET))

    lines += [
        "",
        "// Anything tuned in the headset is written to weapons.cfg by",
        "// 'vrweapon save'. It is exec'd last so it wins, and Setup never",
        "// writes over it - this file is rewritten every run, that one is not.",
        "exec weapons.cfg",
    ]

    with open(path, "w", newline="\r\n") as handle:
        handle.write("\n".join(lines) + "\n")


def write_weapons_stub(path):
    """Create weapons.cfg if it is not there, and never touch it if it is.

    autoexec.cfg exec's this last, so whatever the headset tuning wrote here
    overrides the table above it. Keeping the two in separate files is what lets
    Setup stay free to rewrite its own on every run - which it must, because it
    is the only thing that knows which game is installed - without destroying
    work that can only be done by eye in a headset.
    """
    if os.path.exists(path):
        return

    with open(path, "w", newline="\r\n") as handle:
        handle.write("\n".join([
            "// Weapon offsets tuned in the headset.",
            "//",
            "// Turn 'weapon alignment' on in PC Options, adjust with the off",
            "// hand's stick, then type 'vrweapon save' at the console. This",
            "// file is rewritten by that and by nothing else - Setup leaves it",
            "// alone, so re-running Setup cannot undo any of it.",
            "",
        ]) + "\n")


def write_default_config(path):
    """Team Beef's renderer settings, for a gamedir that has no config yet.

    Only when there is none. The engine rewrites config.cfg every time it
    exits, so on the second run this file is the player's, not Setup's.
    """
    if os.path.isfile(path):
        return

    lines = [
        "// Written by Setup, once, because this gamedir had no config.",
        "// These are Team Beef's renderer settings rather than the engine's",
        "// defaults. Change them in the menus - the engine rewrites this file",
        "// when it exits and Setup will not touch it again.",
        "",
    ]

    for name, value in TEAMBEEF_CONFIG:
        lines.append('set %s "%s"' % (name, value))

    with open(path, "w", newline="\r\n") as handle:
        handle.write("\n".join(lines) + "\n")


def install_game(quake2, dest):
    """Quake II itself. Returns False if its data is not where it should be."""
    base_src = os.path.join(quake2, "baseq2")
    base_dst = os.path.join(dest, "baseq2")
    os.makedirs(os.path.join(base_dst, "save"), exist_ok=True)

    print("Quake II")

    if not copy_file(os.path.join(base_src, "pak0.pak"),
                     os.path.join(base_dst, "pak0.pak"), "pak0.pak"):
        print("  no baseq2/pak0.pak under " + quake2)
        return False

    for name in ("pak1.pak", "pak2.pak", "maps.lst"):
        copy_file(os.path.join(base_src, name), os.path.join(base_dst, name), name)

    # The cinematics, and the multiplayer player models.
    for sub in ("video", "players"):
        copy_tree(os.path.join(base_src, sub), os.path.join(base_dst, sub))

    return True


def install_music(quake2, dest):
    """
    Retail Quake II played its soundtrack off the CD and no download has it.

    The 2023 remaster ships the same tracks as ogg and comes bundled with the
    Steam release, so it is usually right here. That is where Team Beef's music
    folder came from as well - the filenames match exactly.
    """
    src = os.path.join(quake2, "rerelease", "baseq2", "music")
    dst = os.path.join(dest, "baseq2", "music")

    if os.path.isdir(src):
        copy_tree(src, dst)
        print("  soundtrack: %d tracks" % len(os.listdir(dst)))
    elif not os.path.isdir(dst):
        print("  no soundtrack found - the remaster's music folder is the source,")
        print("  and it is not in this install. The game plays fine without it.")


def install_expansions(quake2, dest):
    installed = []

    for gamedir, title, extra in EXPANSIONS:
        pak = os.path.join(quake2, gamedir, "pak0.pak")

        if not os.path.isfile(pak):
            continue

        print(title)
        target = os.path.join(dest, gamedir)
        os.makedirs(os.path.join(target, "save"), exist_ok=True)
        copy_file(pak, os.path.join(target, "pak0.pak"), "pak0.pak")
        copy_tree(os.path.join(quake2, gamedir, "video"),
                  os.path.join(target, "video"))
        write_autoexec(os.path.join(target, "autoexec.cfg"), title, extra)
        write_weapons_stub(os.path.join(target, "weapons.cfg"))
        write_default_config(os.path.join(target, "config.cfg"))

        launcher = os.path.join(dest, "Play %s VR.bat" % title)

        with open(launcher, "w", newline="") as handle:
            handle.write(LAUNCHER.format(args=" +set game " + gamedir))

        installed.append(title)

    return installed


def install_extras(dest):
    """
    Team Beef's own assets, if the owner has their standalone.

    pak6 is 147MB of HD world textures and is inert without gl_retexturing;
    pak99 is the HD viewmodels the weapon offsets assume; vignette.tga is the
    comfort mask. None of it is redistributable and none of it is in a Quake II
    install, so it is picked up only if it is already here or Q2VR_TBDIR points
    at it.
    """
    base_dst = os.path.join(dest, "baseq2")
    source = os.environ.get("Q2VR_TBDIR") or os.path.join(dest, "extras")
    found = []

    for name in EXTRAS:
        if os.path.isfile(os.path.join(base_dst, name)):
            found.append(name)
        elif copy_file(os.path.join(source, name),
                       os.path.join(base_dst, name), name):
            found.append(name)

    if "pak6.pak" in found:
        with open(os.path.join(base_dst, "autoexec.cfg"), "a",
                  newline="\r\n") as handle:
            handle.write('\n// pak6 is inert without this.\nset gl_retexturing "1"\n')

    return found


def main():
    dest = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    quake2 = find_quake2(sys.argv[2] if len(sys.argv) > 2 else None)

    if not quake2:
        print("Could not find Quake II.")
        print()
        print("Set Q2VR_QUAKEDIR to the folder that holds baseq2, or copy")
        print("pak0.pak, pak1.pak and pak2.pak into baseq2 by hand and run this")
        print("again to do the rest.")
        return 1

    print("Quake II found at " + quake2)
    print("Installing into  " + dest)
    print()

    if not install_game(quake2, dest):
        return 1

    install_music(quake2, dest)
    installed = install_expansions(quake2, dest)

    write_autoexec(os.path.join(dest, "baseq2", "autoexec.cfg"), "Quake II", None)
    write_weapons_stub(os.path.join(dest, "baseq2", "weapons.cfg"))
    write_default_config(os.path.join(dest, "baseq2", "config.cfg"))

    with open(os.path.join(dest, "Play Quake II VR.bat"), "w", newline="") as handle:
        handle.write(LAUNCHER.format(args=""))

    found_extras = install_extras(dest)

    print("Menu artwork")

    try:
        subprocess.run([sys.executable,
                        os.path.join(HERE, "make-wheel-icons.py"),
                        quake2, dest], check=True)
    except Exception as exc:
        print("  could not build the weapon wheel icons: %s" % exc)
        print("  Pillow is needed for this:  pip install pillow")
        print("  The game still runs; the wheel will have blank slices.")

    print()
    print("Ready.")
    print("  Quake II" + ("  and " + " and ".join(installed) if installed else ""))

    if not installed:
        print("  No expansions found. If you own The Reckoning or Ground Zero,")
        print("  install them from Steam and run this again.")

    missing = [name for name in EXTRAS if name not in found_extras]

    if missing:
        print()
        print("  Team Beef's extras are not here: " + ", ".join(missing))
        print("  Those are the HD textures, the HD weapon models and the comfort")
        print("  mask from their Quest standalone. They cannot be included and are")
        print("  not in a Quake II install. Without them the game plays the same,")
        print("  with retail artwork. If you have their standalone's data, put")
        print("  those files in an 'extras' folder here and run this again.")

    print()
    print("  Start Virtual Desktop and connect it, then run a Play ... .bat")
    return 0


if __name__ == "__main__":
    sys.exit(main())
