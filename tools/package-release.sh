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
# The game data is put in by tools/setup.py, which is the same script Setup.bat
# runs in a downloaded release - so there is one implementation of "find Quake
# II and copy what is needed", and building a release here exercises exactly the
# path a player takes.
#
# Everything is copied, never moved or linked: the retail game data and Team
# Beef's assets are both read-only sources and are left untouched.
#
# Usage: tools/package-release.sh <output-directory>
#
#   Q2DIR       where Quake II is, if it is somewhere unusual
#   NODATA=1    binaries and tools only, for a release the player will Setup
#
set -euo pipefail

DEST="${1:-}"
if [ -z "$DEST" ]; then
	echo "usage: $0 <output-directory>" >&2
	exit 1
fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build-mingw/release"

if [ ! -f "$BUILD/yquake2.exe" ]; then
	echo "no build found at $BUILD - build first" >&2
	exit 1
fi

echo "packaging into $DEST"
mkdir -p "$DEST/baseq2" "$DEST/tools"

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

# --- the game libraries ----------------------------------------------------
# One per gamedir. The DLLs the retail discs and Steam ship cannot stand in for
# these: Team Beef added three function pointers to game_import_t, so a library
# built against the stock header reads every field after them at the wrong
# offset.
for pack in baseq2 xatrix rogue; do
	if [ -f "$BUILD/$pack/game.dll" ]; then
		mkdir -p "$DEST/$pack"
		cp -f "$BUILD/$pack/game.dll" "$DEST/$pack/"
	fi
done

# --- Setup, and what it needs ----------------------------------------------
cp -f "$REPO/tools/setup.ps1" "$REPO/tools/wheel-art.ps1" "$DEST/tools/"
cp -f "$REPO/tools/setup.py" "$REPO/tools/make-wheel-icons.py" "$DEST/tools/"
cp -f "$REPO/tools/Setup.bat" "$DEST/"

# The icon list is read out of the wheel tables in the engine source, which a
# release does not carry, so it is cached beside the script. Refresh it from
# the source now rather than shipping whatever was there.
if [ -f "$REPO/tools/wheel-icons.txt" ]; then
	cp -f "$REPO/tools/wheel-icons.txt" "$DEST/tools/"
fi

# --- game data -------------------------------------------------------------
if [ "${NODATA:-0}" = "1" ]; then
	# A release to hand to somebody else: binaries and Setup, nothing owned by
	# id Software or Team Beef. The player runs Setup.bat and it is built from
	# their own install.
	echo "  NODATA - binaries and Setup only"
else
	# Team Beef's own assets, for a build made here to play with. Not
	# redistributable and not in anybody's Quake II install: the HD world
	# textures, the HD viewmodels the weapon offsets assume, and the comfort
	# mask. setup.py sees them as already present and leaves them alone.
	for f in pak6.pak pak99.pak vignette.tga; do
		if [ -f "$BUILD/baseq2/$f" ]; then
			cp -f "$BUILD/baseq2/$f" "$DEST/baseq2/"
		fi
	done

	# Their wheel art, which is better than the icons setup.py draws from the
	# retail paks. Whatever is here, setup.py fills in only what is missing.
	if [ -d "$BUILD/baseq2/wheel" ]; then
		cp -rf "$BUILD/baseq2/wheel" "$DEST/baseq2/"
	fi

	python "$REPO/tools/setup.py" "$DEST" ${Q2DIR:+"$Q2DIR"}
fi

# --- readme ----------------------------------------------------------------
cat > "$DEST/README.txt" <<'README'
Quake II VR - PCVR port of Team Beef's Quake2Quest
==================================================

1. Run "Setup.bat" once. It finds your Quake II install, copies the game, the
   expansions you own and the soundtrack out of it, and builds the weapon wheel
   artwork from the same data. It also fetches Team Beef's HD weapon models and
   world textures from their own GitHub release, with their permission - about
   169 MB, once. Nothing has to be installed first: it runs on the PowerShell
   that comes with Windows.

2. Start Virtual Desktop on the headset and connect it to the PC, so that VDXR
   is the running OpenXR runtime.

3. Run "Play Quake II VR.bat".

If Setup cannot find Quake II - installed somewhere unusual, or on another
drive - set Q2VR_QUAKEDIR to the folder containing baseq2 and run it again, or
copy pak0.pak, pak1.pak and pak2.pak into baseq2 by hand and run it again to do
the rest.

Needs a headset with an OpenXR runtime running - Virtual Desktop (VDXR),
SteamVR or the Oculus runtime. With no headset available it falls back to
flatscreen rather than failing, which is handy for checking settings.

This folder is self-contained. Nothing is installed, nothing is read from
outside it, and no toolchain is required. Copy it anywhere and it will run.
Settings, saves and screenshots are written inside the folder, so copying the
folder copies those too - and backing it up backs up everything.

Controls follow Team Beef's standalone. A few that are not obvious:

  Thumbstick click (weapon hand)  cycle laser sight: off / beam / dot
  Grip (weapon hand)              weapon wheel
  Thumbstick down (off hand)      item wheel
  Menu button                     open the menu

VR options are under Options in the main menu, and PC-specific ones - render
resolution, antialiasing, view distance, HUD height and what the desktop window
does - are on the PC Options page below them. Alt+Enter switches the desktop
mirror between a window and full screen.

The expansions
--------------

If you own The Reckoning or Ground Zero, Setup adds them and Single Player
opens a game list. Picking a different game restarts the engine into it, because
the gamedir has to be chosen at startup - so put the headset down for a moment
when you switch.

Team Beef's standalone is base Quake II only, so the VR side of the expansions
is their work applied to code they never shipped. Two things to expect:

- Six of the weapons the expansions add sit where the engine's default offset
  puts them, not where a tuned value would: the Ionripper and Phalanx in The
  Reckoning, and the Disruptor, ETF Rifle, Plasma Beam and Chainfist in Ground
  Zero. Everything they share with Quake II keeps Team Beef's own numbers, and
  so does the Prox Launcher - it uses the Grenade Launcher's model, so their
  value for that one is already right.

  To fix one by eye: Options - PC Options - weapon alignment turns on a readout
  for whatever is in your hand, adjusted with the off hand's stick and the grip
  held for finer steps. Then "vrweapon save" at the console writes the result to
  that game's weapons.cfg, which Setup never overwrites. The values are back,
  left, up, pitch, yaw and roll, and each expansion's autoexec.cfg lists its own
  weapons by number if you would rather type them.

The 2023 remaster's extra episodes - Call of the Machine, Quake II 64 - are not
here and cannot be: they need the remaster's own engine.

Notes
-----

- Team Beef's HD weapon models and world textures are not included and are not
  in a Quake II install. Without them the game plays the same with retail
  artwork. If you have their standalone's data, put pak6.pak, pak99.pak and
  vignette.tga in an "extras" folder here and run Setup again.
- Music comes from the 2023 remaster's soundtrack, which the Steam release
  bundles - Setup copies it if it is there. Retail Quake II played it off the CD.
- Multisampling and vsync are hidden from the video menu while in VR. Both need
  a video restart, which would drop the headset session, and neither affects
  what the headset sees.

Known issues
------------

- On a Quest 2, the id logo, the opening cutscene and the first menu show double.
  A Quest 3 is fine on the same build. Not yet understood.
- The id logo movie at startup is dismissed with the menu button. Any other
  button skips a cutscene once you are in a level, but not that one - it plays
  before there is a game running to skip.
- The picture is slightly darker than Team Beef's standalone. Engine brightness
  is provably identical, so the remaining difference is most likely Virtual
  Desktop's own encode and decode. Its colour settings are the place to look.
README

echo
echo "packaged: $(du -sh "$DEST" | cut -f1)"
echo "launch with: $DEST/Play Quake II VR.bat"
