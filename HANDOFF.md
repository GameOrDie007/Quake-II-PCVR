# Handoff — 2026-09-03 (second)

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

Defects A and B from the previous handoff, both fixed, both desk-verified,
neither headset-verified. Everything is still behind `vr_menu_in_world`, which
defaults to 0.

### A. The pause menu now has a per-eye offset

`menu.c` had no `SCR_GetStereoHudOffset` call and never needed one - menus went
to the flat screen layer, where both eyes see one quad. Kept in the world they
drew at identical coordinates in both eye buffers, so they would not fuse.

The offset is now applied in **one place**, because `M_Draw()` takes no
arguments and menu drawing is spread over ~60 call sites in `menu.c`, `qmenu.c`
and `videomenu.c`. `Draw_SetStereoOffset()` (`src/client/vid/vid.c`) holds a
pixel shift that the `Draw_*` wrappers add to x; `cl_screen.c` sets it right
before `M_Draw()` and zeroes it right after.

The menu is placed at **`vr_screen_depth` (3.5m), not `vr_hud_depth` (0.5m)** -
`SCR_GetStereoMenuOffset` in `cl_screen.c`. That is where the screen layer has
always hung the menu, and a near-fullscreen panel at 0.5m would be at arm's
length. **This is the number most likely to need changing after a headset
session** - if the menu feels wrong, that ratio is the single knob.

### B. The world is no longer blacked out

`M_Draw` skips `Draw_FadeScreen()` when `VR_MenuInWorld()`. Photographed at the
desk first: the menu stays legible without it, because the main menu items are
opaque plaques rather than bare text.

If a headset disagrees, a partial fade needs an alpha argument threaded through
`refexport_t` to `RDraw_FadeScreen` in gl1, gl3 and soft. Only gl1 and soft
ship, but the struct is shared by all three.

### What A does not cover

- `R_RenderFrame` at `menu.c:5251`, the spinning player model on Multiplayer ->
  Player Setup. An x shift would move the viewport and not the model in it.
- `M_Popup()` + `R_EndFrame()` at `menu.c:1421` and `:3526` - the sound-restart
  and server-search messages force a buffer swap from inside a key handler,
  outside the bracket. Already hostile to a stereo path before this change.

### How it was verified without a headset

`TBXR_IsRunning()` is false with no headset, so `VR_MenuInWorld()` cannot be
true and a flatscreen run is a no-regression test. Driven with a generated
config - `map base1`, 200 `wait`s, `menu_main`, `screenshot`, `quit` - run as
`+exec dbgshot.cfg`.

- A temporary forced offset of +40px moved the plaque, the logo, the cursor, all
  five menu items and the highlight, and moved neither the world nor the HUD.
  That is the proof that nothing bypasses the choke point.
- A temporary `Com_Printf` probe read the offset each frame: 0 with the menu up,
  the HUD's own +-507 during play.
- Flatscreen is unchanged, measured: the menu band differs from the pre-change
  build by at most 24 out of 765, where a 1px shift of the same image gives 163.
- The fade still fires flatscreen: world luminance 3.74 against 18.73 unfaded.

## What a headset session should check

In `E:\Games\Quake II VR`, launched **without** `+set vr_weapon_tune 1`:

```
"E:\Games\Quake II VR\yquake2.exe" -portable +set vr_menu_in_world 1
```

1. **Does the pause menu fuse?** Open a menu in game. It should read as one
   menu at a comfortable distance, not two overlapping copies.
2. **Is 3.5m the right distance for it?** This is the judgement call. If it
   feels too far or too near, say which - `SCR_GetStereoMenuOffset` is one
   expression.
3. **Is the world bright enough, and the menu still readable over it?**
4. Then the three things from the previous handoff that have still never run in
   a headset, below.

## Still open

- **The game select page and `relaunchgame` have never run in a headset.**
  Session teardown and rebuild in a new process is the single riskiest untested
  thing in the project. From 2026-08-28.
- **Six weapons need tuning by eye**: Ionripper and Phalanx (The Reckoning);
  Disruptor, ETF Rifle, Plasma Beam, Chainfist (Ground Zero). The Prox Launcher
  does **not** - `v_plaunch` is `v_launch` reskinned (same 208 verts, 384 tris,
  66 frames, byte-identical vertex data in all 66), so it takes the Grenade
  Launcher's tuned value. Team Beef's offsets cannot be derived from geometry -
  7 of their 11 sit at the engine default and the variation is almost all in the
  left/right term. Do not try to fit a model to them again.
- **The weapon alignment tuner's stick input has never been exercised.** Nothing
  has ever moved that stick. If it does nothing in the headset,
  `q2xr_WeaponTuneInput` in `src/vr/vr_surface.c` is where to look.
- **The Plasma Beam fix is unconfirmed.** The beam should lie exactly along the
  laser sight line; they share an origin and a recoiled aim now, so a divergence
  is a real bug.
- **Tier two, if wanted:** making the paused menu hang stationary in the world
  rather than following the gaze needs it rendered into its **own alpha
  swapchain** on a quad layer beside the projection layer. `layers[]` in
  `vr_surface.c` is already an array with a `layerCount`, and
  `q2xrScreenLayerPose` is already a world-locked pose in `StageSpace` at
  `vr_screen_depth`. What is missing is rendering only `M_Draw()` into a
  separate transparent target. Defect A was the prerequisite and is done.
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

## Traps that cost time

**Running the game from the build tree leaves a hung `yquake2.exe` behind on
every `quit`.** It releases its file handles but never exits, and
`Stop-Process -Force` will not shift it. The next link then fails with
`cannot open output file release\yquake2.exe: Permission denied`. Rename the exe
out of the way and link again - Windows will rename a running image quite
happily. Four accumulated over four runs this session; they have to be closed by
hand.

**Screenshot comparison cannot prove flatscreen is unchanged by equality.** Two
runs with identical command lines are byte-identical, which makes the method
look sound - but passing `0` versus `0.0` for the same cvar produces different
images, because the command-line string shifts the animation phase by a frame.
Compare with a threshold and a sensitivity control (shift the same image by one
pixel and measure that), or instrument with a `Com_Printf` probe and read the
value. See [[vr-port-dump-the-buffer]] and [[vr-port-verify-before-asking]] in
the owner's memory.

**The Bash tool's heredocs eat backslashes**, which corrupted a `\n` into a
literal newline inside a C string literal twice in the previous session. Write
patch scripts with the Write tool, not a heredoc. The Bash tool's working
directory also persists across calls and drifts after a `cd`, which silently
turned one `ninja -C build-mingw` into a no-op; use absolute paths.

## How to resume

1. Read this file, then `git log --oneline -15`.
2. The working tree should be clean at the commit that added this file.
3. The owner is an expert headset tester and not a programmer - give him
   commands to run and things to look at, never code to read. Headset sessions
   are the scarce resource; verify everything possible at the desk first.
