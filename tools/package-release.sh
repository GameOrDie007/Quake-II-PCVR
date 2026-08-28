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
# The folder holding baseq2/, and beside it xatrix/ and rogue/ if the owner has
# the mission packs. Overridable, because a Steam install is not where this
# machine keeps its copy.
Q2DIR="${Q2DIR:-}"
if [ -z "$Q2DIR" ]; then
	for d in "/e/Games/Quake 2" 		"/c/Program Files (x86)/Steam/steamapps/common/Quake 2" 		"/c/Program Files/Steam/steamapps/common/Quake 2" 		"/c/GOG Games/Quake 2"; do
		if [ -f "$d/baseq2/pak0.pak" ]; then
			Q2DIR="$d"
			break
		fi
	done
fi
RETAIL="$Q2DIR/baseq2"

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

# --- the mission packs -----------------------------------------------------
#
# The Reckoning and Ground Zero are separate gamedirs. Each needs three things
# beyond its pak: the game library built here (the one Steam ships is 32-bit
# and, more to the point, predates the three function pointers Team Beef added
# to game_import_t), its own wheel icons, and its own autoexec.cfg and
# config.cfg - the engine execs each of those once, taking the first match on
# the search path, so a gamedir that had neither would silently lose Team
# Beef's settings rather than inherit them.
for pack in xatrix rogue; do
	case "$pack" in
		xatrix) title="The Reckoning" ;;
		rogue)  title="Ground Zero" ;;
	esac

	if [ ! -f "$Q2DIR/$pack/pak0.pak" ]; then
		echo "  $title not installed - skipped"
		continue
	fi

	echo "  $title"
	mkdir -p "$DEST/$pack/save"
	cp -f "$BUILD/$pack/game.dll" "$DEST/$pack/"
	cp -f "$Q2DIR/$pack/pak0.pak" "$DEST/$pack/"
	[ -d "$Q2DIR/$pack/video" ] && cp -rf "$Q2DIR/$pack/video" "$DEST/$pack/"

	# Team Beef's settings, which are not optional - gl1_stereo and
	# gl_retexturing among them - plus their weapon offsets. Both games keep
	# baseq2's WEAP_ numbering for the eleven weapons they share, so those
	# values are still right here; only the new weapons need their own.
	[ -f "$DEST/baseq2/config.cfg" ] && cp -f "$DEST/baseq2/config.cfg" "$DEST/$pack/"
	if [ -f "$DEST/baseq2/autoexec.cfg" ]; then
		cp -f "$DEST/baseq2/autoexec.cfg" "$DEST/$pack/autoexec.cfg"
	else
		: > "$DEST/$pack/autoexec.cfg"
	fi

	cat >> "$DEST/$pack/autoexec.cfg" <<PACKCFG

// ---------------------------------------------------------------------------
// $title's own weapons.
//
// These are not Team Beef's numbers - they never shipped this game. They start
// at the engine's default and are meant to be tuned by eye in the headset:
//
//     set vr_weapon_adjustment_<n> "back,left,up,pitch,yaw,roll"
//
// The eleven weapons this game shares with Quake II keep Team Beef's values
// above, which are still correct because $title uses the same WEAP_ numbering
// for them. It has no HD viewmodels of its own, though, so these will not sit
// quite like the ones above until they are tuned.
PACKCFG

	if [ "$pack" = "xatrix" ]; then
		cat >> "$DEST/$pack/autoexec.cfg" <<'PACKCFG'
//WEAP_PHALANX
set vr_weapon_adjustment_12 "10.0,7.0,-8.0,-3.0,0.0,0.0"
//WEAP_BOOMER - the Ionripper
set vr_weapon_adjustment_13 "10.0,7.0,-8.0,-3.0,0.0,0.0"
PACKCFG
	else
		cat >> "$DEST/$pack/autoexec.cfg" <<'PACKCFG'
//WEAP_DISRUPTOR
set vr_weapon_adjustment_12 "10.0,7.0,-8.0,-3.0,0.0,0.0"
//WEAP_ETFRIFLE
set vr_weapon_adjustment_13 "10.0,7.0,-8.0,-3.0,0.0,0.0"
//WEAP_PLASMA - the Plasma Beam
set vr_weapon_adjustment_14 "10.0,7.0,-8.0,-3.0,0.0,0.0"
//WEAP_PROXLAUNCH
set vr_weapon_adjustment_15 "10.0,7.0,-8.0,-3.0,0.0,0.0"
//WEAP_CHAINFIST
set vr_weapon_adjustment_16 "10.0,7.0,-8.0,-3.0,0.0,0.0"
PACKCFG
	fi
done

# Wheel icons for whichever packs were installed, built from the owner's own
# paks. Only the weapons the packs add need them; everything shared resolves to
# Team Beef's art in baseq2/wheel through the search path.
if command -v python >/dev/null 2>&1; then
	python "$REPO/tools/make-wheel-icons.py" "$Q2DIR" "$DEST" || \
		echo "  warning: wheel icons not generated" >&2
else
	echo "  warning: no python - wheel icons not generated" >&2
fi

# --- launcher --------------------------------------------------------------
# -portable keeps config.cfg, saves and screenshots inside this folder rather
# than in Documents, which is what makes the folder self-contained.
printf '@echo off\r\nstart "" "%%~dp0yquake2.exe" -portable\r\n' \
	> "$DEST/Play Quake II VR.bat"

# One launcher per game. The gamedir has to be chosen at startup: changing it
# while running ends in vid_restart, which destroys the GL context the OpenXR
# swapchain images belong to, and nothing brings the session back.
for pack in xatrix rogue; do
	case "$pack" in
		xatrix) title="The Reckoning" ;;
		rogue)  title="Ground Zero" ;;
	esac
	if [ -f "$DEST/$pack/pak0.pak" ]; then
		printf '@echo off\r\nstart "" "%%~dp0yquake2.exe" -portable +set game %s\r\n' \
			"$pack" > "$DEST/Play $title VR.bat"
	fi
done

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

The mission packs
-----------------

"Play The Reckoning VR.bat" and "Play Ground Zero VR.bat" are there if you own
the expansions - the packaging step picked them up from the same Quake II
install as the main game.

Team Beef's standalone is base Quake II only, so the VR side of these is their
work applied to code they never shipped. Two things to expect:

- The nine weapons the expansions add sit where the engine's default offset puts
  them, not where a tuned value would. Everything they share with Quake II keeps
  Team Beef's own numbers. To adjust one, at the console:

      set vr_weapon_adjustment_13 "10.0,7.0,-8.0,-3.0,0.0,0.0"

  which is back, left, up, pitch, yaw, roll. Each expansion's autoexec.cfg lists
  its own weapons by number.

- Ground Zero's Plasma Beam is drawn from the head rather than the gun. What it
  hits is correct; where the beam appears to start is not, yet.

Each game is a separate launcher because the gamedir is fixed at startup.

The 2023 remaster's extra episodes - Call of the Machine, Quake II 64 - are not
here and cannot be: they need the remaster's own engine.
README

echo
echo "packaged: $(du -sh "$DEST" | cut -f1)"
echo "launch with: $DEST/Play Quake II VR.bat"
