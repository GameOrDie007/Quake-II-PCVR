# Quake II VR — PCVR

A PC port of [Team Beef](https://www.teambeef.games/)'s **Quake2Quest**, the
Quest standalone VR build of Quake II. Runs on Windows through OpenXR against
any runtime — Virtual Desktop (VDXR), SteamVR, or the Oculus runtime.

The goal was reproduction, not reinterpretation. Team Beef designed and tuned
the VR experience; this moves it to PC hardware and changes as little else as
possible.

## Lineage and credit

- **Quake II** — id Software, GPLv2.
- **[Yamagi Quake II](https://github.com/yquake2/yquake2)** — the engine this
  is built on. Specifically **7.41**, because that is the exact version
  Quake2Quest forked.
- **[Quake2Quest](https://github.com/DrBeef/Quake2Quest)** — Team Beef's VR
  fork. The VR design, the input model, the weapon handling and the tuned
  values are all theirs.

This port is GPLv2, as everything above it is. Every file added here is under
the same licence, and the complete corresponding source is this repository.

Yamagi Quake II's own README, which documents the engine rather than this port,
is kept as [README-yquake2.md](README-yquake2.md). Building is in
[BUILD-WINDOWS.md](BUILD-WINDOWS.md).

## How faithful is it?

Measured rather than asserted:

- Team Beef's engine changeset applies **verbatim** — 62 files, 6,936 lines,
  zero rejected hunks. Building on 7.41 rather than current upstream is what
  makes that possible.
- Their VR layer is **byte-identical** apart from include paths.
  `VrInputCommon.c`, `mathlib.c`, `matrixlib.c`, `VrInput.h` and `VrCvars.h`
  differ by zero lines. `VrInputDefault.c` carries one documented correction.
- Their cvar set matches exactly — diffing their shipped `config.cfg` against
  this build shows no VR option missing.

Two builds are tagged:

| tag | what it is |
|---|---|
| `quake2-vr-1to1-r2` | their game on PC, nothing added |
| `quake2-vr-pc` | the above plus a PC Options screen |

The PC options default to Team Beef's values, so an untouched install of
either renders identically.

What the PC branch adds on top:

* **The Reckoning and Ground Zero**, playing in VR rather than merely loading.
* **A game select page** in front of Single Player, listing whichever games
  are installed.
* **A PC Options page** - render resolution, antialiasing, extended view
  distance, HUD height, and what the desktop window does.
* **A desktop mirror worth streaming** - borderless full screen by default,
  Alt+Enter to windowed and back, resizable, and cropped to the shape of the
  window rather than squashed into it.

Every added option defaults to Team Beef's own value where they have one, so an
untouched install behaves exactly as their game does.

## What changed, and why

Almost every difference is a platform seam rather than a design change. The
interesting ones:

- **OpenXR replaces their Android layer.** `Q2VR_SurfaceView.c` owns the JNI
  surface, EGL context and app thread; `src/vr/vr_surface.c` is its PC
  counterpart, reproducing the same action set, frame structure and pose maths.
- **The VR layer drives the engine, not the reverse.** Team Beef commented out
  the `Qcommon_Mainloop` call so the platform renders `Qcommon_Frame` once per
  eye inside one `xrBeginFrame`/`xrEndFrame` pair. The Windows backend does the
  same.
- **Bring-up is split in two.** Instance and view configuration before
  `Qcommon_Init` (the engine needs the eye resolution while starting), session
  and swapchains after, once a GL context exists.
- **Multisampling resolves explicitly.** Theirs uses a GLES extension that
  resolves implicitly into the swapchain; desktop GL has no equivalent, so the
  same sample count is reached with a multisample framebuffer and a blit.
- **`gl1_stereo 8`**, `r_mode -1` and the eye dimensions are set in code. Team
  Beef pass these on a command line their Android launcher builds — which is
  invisible in their source and load-bearing: without it the image will not
  fuse.
- **Culling uses the headset's field of view.** `R_SetFrustum` culls against
  the player state's symmetric 90°, while the projection uses OpenXR's wider
  asymmetric FOV. Harmless at a Quest's per-eye FOV, visible through VDXR as
  world geometry vanishing at the edge of vision.

Things Android never exercised and so were never wrong for them: no dedicated
server, no keyboard, and no desktop window to present to.

## Building

Windows, MinGW-w64 via MSYS2. **MSVC will not work** — 7.41 uses C99
variable-length arrays in 45 places, and editing engine source to avoid them
defeats the point of the exercise.

```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
          mingw-w64-x86_64-SDL2 mingw-w64-x86_64-openal \
          mingw-w64-x86_64-openxr-loader

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build-mingw
ninja -C build-mingw
```

`-fcommon` is required and set by the build: GCC 10 changed its default, and
7.41 relies on the old behaviour.

`tools/package-release.sh <dir>` assembles a self-contained, portable folder.

## Installing

A release is binaries and a script - about 16MB, with no game data in it.

1. Extract it anywhere.
2. Run **`Setup.bat`** once.
3. Run **`Play Quake II VR.bat`**.

Setup finds your Quake II install, copies the game, whichever expansions you
own and the soundtrack out of it, and builds the weapon wheel artwork from
the same data. There is nothing to install first: it runs on the PowerShell
that ships with Windows.

It makes exactly one network request, and only if it needs to - see **Team
Beef's assets** below. Nothing else is downloaded and nothing about your
machine is sent anywhere.

`tools/setup.py` and `tools/make-wheel-icons.py` are the same job in Python and
are what the repository uses to build a release. The two are kept in step by
running both and comparing what they produce - every generated file byte for
byte, and all 80 wheel images pixel by pixel.

If Setup cannot find Quake II, set `Q2VR_QUAKEDIR` to the folder holding
`baseq2` and run it again, or copy the paks in by hand and run it again to do
the rest.

The soundtrack comes from the 2023 remaster's `music` folder, which the Steam
release bundles - retail Quake II played it off the CD and no download has it.
That is also where Team Beef's music came from; the filenames match exactly.

### Team Beef's assets

`pak6.pak` is 147MB of HD world textures, `pak99.pak` the HD viewmodels the
weapon offsets were tuned against, and `vignette.tga` the comfort mask. They
are **Team Beef's own work**, they are not in a Quake II install, and none of
them ships here.

Setup looks for them on your machine first - an `extras` folder beside the game,
or a copy already installed. If they are still missing it downloads **their
Android release** from
[their own release page](https://github.com/DrBeef/Quake2Quest/releases) and
takes just those three files out of it. About 169MB, once. Nothing is
redistributed by this project and the download is discarded afterwards.

To skip it entirely:

```
Setup.bat -Extras no
```

Without them the game plays the same with retail artwork - the wheel icons are
generated from your own paks, and the comfort mask is skipped rather than drawn
as a missing texture. If the download fails, Setup says so and carries on.

The folder is self-contained: config, saves and screenshots are all written
inside it, so backing it up backs up everything and copying it to another PC
carries your settings along.

## The mission packs

Both official expansions play in VR. Team Beef's standalone is base Quake II
only, so this is not a port of anything of theirs - it is their VR changeset
applied to yquake2's own ports of the mission pack game code, `XATRIX_2_06` and
`ROGUE_2_05`, the releases current when yquake2 7.41 shipped.

You need your own copy of each. `tools/package-release.sh` picks them up from
the same install it takes Quake II from and writes a launcher for each.

The DLLs Steam and the discs ship cannot be used, and not only because they are
32-bit: Team Beef added three function pointers to `game_import_t`, so a game
library built against the stock header reads every field after them at the wrong
offset. They also changed `shared.h` in ways that move every field the engine
and a game library pass between them - `player_state_t` is 208 bytes here where
stock's is 184. Each pack's `shared.h` is therefore a copy of the engine's with
only the include guard renamed, so the layouts cannot drift.

What is worth knowing before playing:

* **Six new weapons have untuned offsets.** Every weapon the packs share with
  Quake II keeps Team Beef's tuned `vr_weapon_adjustment` values, because both
  packs use the same `WEAP_` numbering for those. The Prox Launcher gets theirs
  too: `v_plaunch` is `v_launch` reskinned - same vertices, triangles and
  frames, byte for byte - so the Grenade Launcher's value is right for it. The
  Ionripper, Phalanx, ETF Rifle, Plasma Beam, Chainfist and Disruptor start at
  the engine's default and want adjusting by eye.

  Turn on **weapon alignment** in PC Options and the offsets for whatever is in
  your hand appear in the game, adjusted with the off hand's stick - held grip
  for finer steps. It draws in the game rather than on a menu page because a
  menu drops both eyes onto a flat quad and stops the weapon tracking your hand,
  which is the one thing that has to be judged. `vrweapon save` at the console
  writes the result to the gamedir's `weapons.cfg`, which Setup never
  overwrites, so re-running Setup cannot undo it.
* **The Plasma Beam draws from the face.** Its start point is computed
  client-side from `cl.refdef.vieworg` plus `gunoffset`, and `gunoffset` is zero
  in VR. The damage trace is already correct; the beam is not.
* **The weapon wheels are checked, not assumed.** `vrwheel` at the console
  prints each segment beside the server's own name for that inventory index and
  says whether the icon loads. All 46 segments across both packs verify.

Single Player opens a game list when more than one is installed, and picking
a different one restarts the engine into it - `relaunchgame` rebuilds the
command line, and the new instance waits on the old one's pid before touching
OpenXR. It restarts rather than switching in place because a gamedir change
ends in `vid_restart`, and `VID_Shutdown` destroys the GL context the OpenXR
swapchain images belong to. Each game also keeps its own launcher.

**The 2023 remaster's content is not reachable from here.** Call of the Machine
uses the extended `QBSP` map format - `maps/mgu1m1.bsp` in the remaster's pak
begins `QBSP`, not `IBSP` - which yquake2 only learned to read in 8.x, and its
gameplay lives in KEX game code with monsters and entities this lineage does not
have. Reaching it would mean giving up the 7.41 base that makes Team Beef's
changeset apply verbatim.

## What Team Beef's assets are worth

Setup builds a complete install without them, but they are most of what their
standalone looks like, and if you have their data it is worth putting in:

- **`pak99.pak`** — HD weapon models. Their viewmodels have no arm, which is
  why the standalone shows just the gun, and the `vr_weapon_adjustment` values
  Setup writes were tuned against these. With retail models the alignment is
  close rather than exact.
- **`pak6.pak`** — 147MB of HD world textures. **Inert unless `gl_retexturing`
  is `1`**, which Setup writes to `autoexec.cfg` only when the pak is there.
- **`vignette.tga`** — the comfort mask. Skipped when absent rather than drawn
  as a missing texture.
- **`wheel/`** — their weapon wheel art. Setup draws its own from your paks
  where theirs is missing, and prefers theirs wherever it is present.

## Known

- On a Quest 2, the id logo, the opening cutscene and the first menu show double.
  A Quest 3 is fine on the same build. All three are `useScreenLayer()` cases,
  where the scene is rendered once onto a quad, so ordinary stereo disagreement
  should be impossible - it is not the same family as the menu fusing bug, and it
  is not yet understood.
- The id logo movie at startup is dismissed with the menu button rather than any
  button. It plays before there is a server connection, and the skip works by
  telling the server to move on.
- Six of the weapons the mission packs add have untuned offsets - see above. The
  in-game weapon alignment page adjusts them.
- The standalone renders slightly darker. Engine-side brightness is provably
  identical — `gammatable` is identity in both, intensity is 3.7 in both, and
  no lighting cvar differs. The remaining difference is most likely Virtual
  Desktop's encode/decode path, which is the one part of the comparison that is
  not the same pipeline.
- `gl3` is not built. Team Beef's VR work is entirely in `gl1`, and their `gl3`
  was never made to compile.
