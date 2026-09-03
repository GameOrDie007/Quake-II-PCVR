# Handoff — 2026-09-03

Written to hand this work to a fresh session. Read this first, then
`PROGRESS.md` for the deeper history and `README-VR.md` for the player-facing
picture.

## Where things are

- Repo: `E:\Tools\Games\Quake2VR-741`, branch `vr-741-base`.
- Build: from the repo root, with MSYS2's gcc on PATH and VS Build Tools' ninja:

  ```
  export PATH="/c/msys64/mingw64/bin:$PATH"
  "/c/PROGRA~2/MICROS~2/18/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -C build-mingw
  ```

  MSVC will not work - 7.41 uses C99 VLAs in 45 places.
- Test install (the one the owner wears a headset for):
  **`E:\Games\Quake II VR`**. Not `(packs)`, not the build tree. He has tested
  the wrong folder before, so name it explicitly every time.
- The build tree has no `pak0.pak`, so run it with
  `-portable -datadir "E:\Games\Quake II VR"`.

## What landed this session

Three pieces, all built and desk-verified, none headset-verified except where
said. Every one is off by default so an untouched install behaves as Team
Beef's does.

### 1. Plasma Beam leaves the gun, not the face

`src/client/cl_tempentities.c`. The bug was two head-based assumptions, not the
one previously recorded: the start point came from `vieworg + gunoffset` (zero
in VR) *and* the basis copied into `f/r/u` was the view's, which further down
replaced the beam's direction with wherever the head was looking. Both now come
from the weapon. The laser sight's origin maths was extracted to
`CL_VRGunOrigin()` and is shared, so the two cannot drift.

**Headset check:** the beam should lie exactly along the laser sight line. They
now use the same origin and the same recoiled aim, so a divergence is a real
bug.

### 2. Weapon alignment tuner

PC Options -> `weapon alignment`. Draws a readout in the game (not a menu page -
see the note under "Why menus were the hard part") listing the six offset
values for whatever is in hand, adjusted with the off hand's stick, grip held
for finer steps. `vrweapon save` at the console writes them out.

- Input lives in `src/vr/vr_surface.c` (`q2xr_WeaponTuneInput`), on our side of
  the seam - it reads the off hand's stick and zeroes it before Team Beef's
  `HandleInput_Default` sees it, so you do not walk while tuning but everything
  else stays theirs.
- Labels are Team Beef's own words from their `autoexec.cfg` header -
  "backwards, left, up, pitch (down), yaw, roll". Read `SetWeapon6DOF` alone and
  the first axis looks like forward; the 180-degree pitch flip inverts it. Do
  not "fix" these labels.
- **Verified at the desk:** the readout draws, reads the correct weapmodel
  index, parses the right cvar, and shows Team Beef's real values.
  `vrweapon save` writes correctly and does not touch the owner's install.
- **Not verified:** nothing has ever moved that stick. If it does nothing in the
  headset, `q2xr_WeaponTuneInput` is where to look.

### 3. Pause without leaving VR

PC Options -> `pause without leaving vr` (`vr_menu_in_world`, default 0).
Keeps the world in its projection layer while an in-game menu is open. Four
sites all ask one function, `VR_MenuInWorld()` in `vr_surface.c`:

- `useScreenLayer()` no longer collapses the world to a flat quad for this case.
- `CL_PredictMovement` (`cl_prediction.c`) no longer freezes the view when
  paused. `cl.viewangles` was always live - `CL_RefreshCmd` runs regardless of
  pause - but the prediction that copies it into `cl.predicted_angles`, which is
  what the renderer reads, bailed out.
- `CL_AddViewWeapon` hides the viewmodel while paused, because Team Beef update
  the weapon pose only in the gameplay half of `HandleInput_Default` and it
  would otherwise ride along welded to the face.
- `SCR_GetStereoHudOffsetScaled` gives the per-eye offset in this case.

Gated on `TBXR_IsRunning()`, so flatscreen is untouched whatever the cvar says.

**Owner reported after testing:** it does stay in stereo, the head does move the
view, and the menu follows the gaze as expected.

## Open defects, from the owner's headset session

All three are from the pause-in-world feature, reported 2026-09-03. Diagnosed
but **not fixed**.

### A. The pause menu is double vision — the real one

`src/client/menu/menu.c` contains **zero** calls to `SCR_GetStereoHudOffset`.
It never needed one: menus always went to the flat screen layer, where both eyes
see one quad. Now the menu draws into both eye buffers at identical screen
coordinates, so the eyes disagree and it will not fuse.

The fix is to offset menu drawing per eye. The primitives are `M_DrawCharacter`
(`menu.c:420`), `M_DrawPic` (`:452`) and `M_DrawCursor` (`:466`), but there are
also direct `Draw_PicScaled` / `Draw_CharScaled` calls scattered through -
`:112` (banner), `:662`-`:676` (main menu), `:948` (cursor). Note `M_Draw()` is
called with no arguments (`cl_screen.c:2514`), so `separation` is not threaded
in; it needs either a parameter or a module-level offset set before the call.
Match what the HUD already does rather than inventing a second scheme.

### B. The background dims too much

`menu.c:5408` calls `Draw_FadeScreen()` over the whole framebuffer. Correct when
that framebuffer is a flat panel; in world it dims the entire world. Skip it, or
lighten it substantially, when `VR_MenuInWorld()`.

### C. The tuner readout is always on screen

**Not a bug.** The launch command handed to the owner included
`+set vr_weapon_tune 1`, so it was on from spawn in every game. Relaunch without
it, or turn it off in PC Options. Worth considering whether it should refuse to
draw in `baseq2`, where nothing needs tuning.

## Still open from before this session

- **The headset session has not happened yet** for the mission-pack work from
  2026-08-28. In particular the **game select page and `relaunchgame`** have
  never run in a headset - session teardown and rebuild in a new process is the
  single riskiest untested thing in the project.
- **Six weapons need tuning by eye**: Ionripper and Phalanx (The Reckoning);
  Disruptor, ETF Rifle, Plasma Beam, Chainfist (Ground Zero). The Prox Launcher
  does **not** - `v_plaunch` is `v_launch` reskinned (same 208 verts, 384 tris,
  66 frames, byte-identical vertex data in all 66), so it takes the Grenade
  Launcher's tuned value. Team Beef's offsets cannot be derived from geometry -
  7 of their 11 sit at the engine default and the variation is almost all in the
  left/right term. Do not try to fit a model to them again.
- **Not published.** There is no `origin` remote, only `upstream` yquake2. The
  Quake port shipped to github.com/GameOrDie007/Quake-PCVR on 2026-08-29; this
  one never did.
- **The release zip on disk is stale.** `E:\Games\Quake2VR-vr-741-base-0717e03a.zip`
  predates the ship-audit commit and is missing LICENSE. Rebuild with
  `tools/make-dist.py` from HEAD before handing anything out.

## Why menus were the hard part

Worth knowing before touching any of this. Team Beef key two behaviours off
`cls.key_dest`, and both bite on PC:

1. `useScreenLayer()` puts the whole scene on a flat quad whenever
   `key_dest != key_game`.
2. `HandleInput_Default` (`VrInputDefault.c:116`) only updates the weapon pose
   in its `else` branch, so the gun freezes the moment a menu opens.

Together these mean **a menu page can never show a gun tracking your hand**,
which is why the tuner draws in the game instead. Neither shows on a Quest,
where there is no desktop and every menu is a flat panel by design.

## Tier two, if wanted

Making the paused menu hang stationary in the world rather than following the
gaze needs the menu rendered into its **own alpha swapchain** and hung on a quad
layer beside the projection layer. The machinery is already there: `layers[]` in
`vr_surface.c` is an array with a `layerCount`, and `q2xrScreenLayerPose` is
already a world-locked pose in `StageSpace` at `vr_screen_depth`. What is
missing is rendering only `M_Draw()` into a separate transparent target. Fixing
defect A first is a prerequisite either way.

## A trap that cost time this session

**Screenshot comparison cannot prove flatscreen is unchanged here.** Two runs
with identical command lines are byte-identical, which makes the method look
sound - but passing `0` versus `0.0` for the same cvar produces different
images, because the command-line string shifts the animation phase by a frame.
A pixel diff between two runs proves nothing. Instrument with a `Com_Printf`
probe and read the value instead. See [[vr-port-dump-the-buffer]] and
[[vr-port-verify-before-asking]] in the owner's memory.

Also: **the Bash tool's heredocs eat backslashes**, which corrupted a `\n` into
a literal newline inside a C string literal twice this session. Write patch
scripts with the Write tool, not a heredoc.

## How to resume

1. Read this file, then `git log --oneline -15`.
2. The working tree should be clean at the commit that added this file.
3. The owner is an expert headset tester and not a programmer - give him
   commands to run and things to look at, never code to read. Headset sessions
   are the scarce resource; verify everything possible at the desk first.
