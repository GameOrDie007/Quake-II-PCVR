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

This port is GPLv2, as everything above it is.

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

* **The nine new weapons have untuned offsets.** Every weapon the packs share
  with Quake II keeps Team Beef's tuned `vr_weapon_adjustment` values, because
  both packs use the same `WEAP_` numbering for those. The Ionripper, Phalanx,
  ETF Rifle, Prox Launcher, Plasma Beam, Chainfist and Disruptor start at the
  engine's default and want adjusting by eye - each pack's `autoexec.cfg` names
  them.
* **The Plasma Beam draws from the face.** Its start point is computed
  client-side from `cl.refdef.vieworg` plus `gunoffset`, and `gunoffset` is zero
  in VR. The damage trace is already correct; the beam is not.
* **The weapon wheels are checked, not assumed.** `vrwheel` at the console
  prints each segment beside the server's own name for that inventory index and
  says whether the icon loads. All 46 segments across both packs verify.

There is no game select page in the headset yet; each game has its own launcher.
An in-game switch would need a process relaunch, because changing gamedir ends
in `vid_restart`, which destroys the GL context the OpenXR swapchain images
belong to.

**The 2023 remaster's content is not reachable from here.** Call of the Machine
uses the extended `QBSP` map format - `maps/mgu1m1.bsp` in the remaster's pak
begins `QBSP`, not `IBSP` - which yquake2 only learned to read in 8.x, and its
gameplay lives in KEX game code with monsters and entities this lineage does not
have. Reaching it would mean giving up the 7.41 base that makes Team Beef's
changeset apply verbatim.

## Game data — not included, and not includable

You need your own copy of Quake II, and Team Beef's assets from their APK.
Neither is redistributable here.

Into `baseq2/` alongside the binary:

| from | files |
|---|---|
| your Quake II install | `pak0.pak`, `pak1.pak`, `pak2.pak`, `video/`, `players/` |
| Team Beef's Quake2Quest | `pak6.pak`, `pak99.pak`, `autoexec.cfg`, `music/`, `vignette.tga`, `wheel/` |

Their assets are not optional extras — they are most of what the game looks
like:

- **`pak99.pak`** — HD weapon models. Their viewmodels have no arm, which is
  why the standalone shows just the gun.
- **`pak6.pak`** — 147MB of HD world textures. **Inert unless
  `gl_retexturing` is `1`.** Without it the world looks like plain retail
  Quake II.
- **`autoexec.cfg`** — per-weapon offsets, commented "the default for the HD
  weapon models". The alignment they tuned assumes `pak99`.

The Steam release ships no CD audio, so music comes from their `music/` folder.

## Known

- The standalone renders slightly darker. Engine-side brightness is provably
  identical — `gammatable` is identity in both, intensity is 3.7 in both, and
  no lighting cvar differs. The remaining difference is most likely Virtual
  Desktop's encode/decode path, which is the one part of the comparison that is
  not the same pipeline.
- `gl3` is not built. Team Beef's VR work is entirely in `gl1`, and their `gl3`
  was never made to compile.
