#!/usr/bin/env bash
#
# Assemble a self-contained, portable Quake II VR build.
#
# The result is a single folder that can be copied to any Windows machine with
# an OpenXR runtime and played - no MSYS2, no install step, no registry, and
# nothing read from outside the folder. Config is written next to the binary
# because the launcher passes -portable, so a backup of the folder is a backup
# of the settings and saves too.
#
# Everything is copied, never moved or linked: the retail game data and Team
# Beef's assets are both read-only sources and are left untouched.
#
# Usage: tools/package-release.sh <output-directory>
#
set -euo pipefail

DEST="${1:-}"
if [ -z "$DEST" ]; then
	echo "usage: $0 <output-directory>" >&2
	exit 1
fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build-mingw/release"
RETAIL="/e/Games/Quake 2/baseq2"

if [ ! -f "$BUILD/yquake2.exe" ]; then
	echo "no build found at $BUILD - build first" >&2
	exit 1
fi

if [ ! -f "$RETAIL/pak0.pak" ]; then
	echo "retail Quake II data not found at $RETAIL" >&2
	exit 1
fi

echo "packaging into $DEST"
mkdir -p "$DEST/baseq2"

# --- binaries and the DLLs they need ---------------------------------------
# Nothing here may resolve through PATH: the whole point is that this runs on a
# machine with no toolchain installed.
for f in yquake2.exe quake2.exe ref_gl1.dll ref_soft.dll \
	SDL2.dll libopenxr_loader.dll libgcc_s_seh-1.dll libstdc++-6.dll \
	libwinpthread-1.dll openal32.dll; do
	if [ -f "$BUILD/$f" ]; then
		cp -f "$BUILD/$f" "$DEST/"
	else
		echo "  warning: missing $f" >&2
	fi
done

# --- the game --------------------------------------------------------------
cp -f "$BUILD/baseq2/game.dll" "$DEST/baseq2/"

# Retail data. gamex86.dll is deliberately not copied - that is the stock game
# library, and this build has its own with Team Beef's VR changes in it.
for f in pak0.pak pak1.pak pak2.pak maps.lst; do
	[ -f "$RETAIL/$f" ] && cp -f "$RETAIL/$f" "$DEST/baseq2/"
done
# video/ holds the cinematics and players/ the multiplayer models.
for d in video players; do
	[ -d "$RETAIL/$d" ] && cp -rf "$RETAIL/$d" "$DEST/baseq2/"
done

# Team Beef's assets. pak6 is the HD world textures and is inert without
# gl_retexturing 1; pak99 is the HD weapon models; autoexec.cfg carries the
# per-weapon offsets those models assume.
for f in pak6.pak pak99.pak autoexec.cfg vignette.tga config.cfg; do
	[ -f "$BUILD/baseq2/$f" ] && cp -f "$BUILD/baseq2/$f" "$DEST/baseq2/"
done
for d in music wheel; do
	[ -d "$BUILD/baseq2/$d" ] && cp -rf "$BUILD/baseq2/$d" "$DEST/baseq2/"
done

mkdir -p "$DEST/baseq2/save"

# --- launcher --------------------------------------------------------------
# -portable keeps config.cfg, saves and screenshots inside this folder rather
# than in Documents, which is what makes the folder self-contained.
printf '@echo off\r\nstart "" "%%~dp0yquake2.exe" -portable\r\n' \
	> "$DEST/Play Quake II VR.bat"

# --- readme ----------------------------------------------------------------
cat > "$DEST/README.txt" <<'README'
Quake II VR - PCVR port of Team Beef's Quake2Quest
==================================================

Run "Play Quake II VR.bat".

Needs a headset with an OpenXR runtime running - Virtual Desktop (VDXR),
SteamVR or the Oculus runtime. Start that first, then launch. With no headset
available it falls back to flatscreen rather than failing.

This folder is self-contained. Nothing is installed, nothing is read from
outside it, and no toolchain is required on the machine. Copy it anywhere and
it will run. Settings, saves and screenshots are written inside the folder, so
copying the folder copies those too - and backing it up backs up everything.

Controls follow Team Beef's standalone. A few that are not obvious:

  Thumbstick click (weapon hand)  cycle laser sight: off / beam / dot
  Menu button                     open the menu
  VR options are under Options in the main menu.

Notes:

- The weapon models and world textures are Team Beef's HD replacements. The
  world textures need gl_retexturing at 1, which the bundled config already
  sets - if the world ever looks like plain retail Quake II, check that.
- Music comes from the ogg files in baseq2/music.
- Multisampling and vsync are hidden from the video menu while in VR. Both
  need a video restart, which would drop the headset session, and neither
  affects what the headset sees.
README

echo
echo "packaged: $(du -sh "$DEST" | cut -f1)"
echo "launch with: $DEST/Play Quake II VR.bat"
