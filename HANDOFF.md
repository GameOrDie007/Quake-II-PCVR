# Handoff — Quake II PCVR

**What this is:** what is in flight right now, and how to work in this repo. It
is read at the start of every resumed session, so it is kept short and rewritten
rather than appended to. Finished work is one line at the bottom with its commit;
the reasoning is in `git log`, the deeper history in `PROGRESS.md`, and any
lesson that would help a *different* port belongs in the `vr-port-diagnostics`
skill, not here.

## Where things are

- Repo: `E:\Tools\Games\Quake2VR-741`, branch `vr-741-base`.
- Build, from the repo root, MSYS2 gcc plus VS Build Tools' ninja:

  ```
  export PATH="/c/msys64/mingw64/bin:$PATH"
  "/c/PROGRA~2/MICROS~2/18/BUILDT~1/Common7/IDE/COMMON~1/MICROS~1/CMake/Ninja/ninja.exe" -C build-mingw
  ```

  MSVC will not work — 7.41 uses C99 VLAs in 45 places.
- **Test install: `E:\Games\Quake II VR`.** Not `(packs)`, not the build tree.
  He has tested the wrong folder before, so name it explicitly every time.
- The build tree has no `pak0.pak`; run it with
  `-portable -datadir "E:\Games\Quake II VR"`.
- Staging a build for him is one file — only `yquake2.exe` ever changes:

  ```
  Copy-Item "E:\Tools\Games\Quake2VR-741\build-mingw\release\yquake2.exe" "E:\Games\Quake II VR\yquake2.exe" -Force
  ```

## The desk can run the whole VR path — read this before asking him for anything

`VirtualDesktopXR` is installed, and **while Virtual Desktop is running a second
process gets a real OpenXR session of its own.** Stereo, swapchains, the menu
layer, the input seam — all of it runs at the desk with no headset on a head.
Confirm a run really had a session:

```
grep -a "OpenXR runtime is\|session created\|creating swapchain" build-mingw/release/stdout.txt
```

Three `creating swapchain` lines means the menu layer's swapchain was made too.

Two limits and one trap:

- **No head or controller motion.** Parking the headset so the pose is constant
  is often what makes a comparison readable — that is how item 8 was proved.
- **To stand in for a button press**, add a temporary console command setting a
  one-shot flag the input path treats as a button edge. That is how the cutscene
  skip was verified. Remove it and grep to prove it is gone.
- **A session created but never reaching running freezes the game.** `q2xr_Frame`
  returns early while `gApp.SessionRunning` is false, and it is `q2xr_Frame` that
  calls `Qcommon_Frame` — so nothing ticks at all, no map loads, no frame
  renders. That is also what a player gets launching with Virtual Desktop up and
  the headset asleep, so it is worth fixing on its own account.

## In flight

**Nothing is mid-edit.** The tree is clean; everything below is either awaiting a
headset or not started.

### Awaiting his headset

- **The cutscene skip** (`4db08e25`). He reported he could not skip the starting
  level cutscene at all. Fixed and desk-verified with the fake-press hook, never
  tried by hand. Any face button or the trigger, after the first second.
- **The demo's head steering** (`77a80fe5`). He said "Perfect. It's in world" and
  that the menu pauses and navigates — but was never asked the specific question,
  which is whether turning his head now looks around a *stable* world instead of
  dragging it. Ask outright.
- **The Plasma Beam** (`09b26186`). It should lie exactly along the laser sight
  line; they share an origin and a recoiled aim now, so a divergence is real.
- **The weapon tuner's stick input has never been exercised.** The drawing, the
  cvar plumbing and `vrweapon save` are all proven at the desk; nothing has ever
  moved that stick. If it does nothing, `q2xr_WeaponTuneInput` in
  `src/vr/vr_surface.c` is where to look.
- **The menu's own composition layer** (`vr_menu_in_world 2`, `4823d8df`).
  Everything measurable has been measured; what it *looks* like has not.
- **HUD spacing** (`d72929a5`). The overlap is gone, but the spacing has only
  been judged at the desk.
- **The game select page and `relaunchgame` have never run in a headset.**
  Tearing an OpenXR session down and rebuilding it in a new process is the single
  riskiest untested thing in the project. Outstanding since 2026-08-28.

**The startup id movie still cannot be skipped by a button**, and this fix will
not do it: no server connection, and `SCR_FinishCinematic` works by writing
`nextserver` into the netchan. Dismissing it still goes through the menu.

### Diagnosed, not measured — and this is a desk job

**2. Snap turn throws the weapon to one side for a frame.** "The controller stays
on one side of the screen for a split second and then appears in its proper
place." The correction for exactly this is **already in the tree** —
`VrInputDefault.c`, `snapTurnAtEntry` and the block at the end, gated on
`vr_smoothturn == 0`, ported from the older `Quake2VR` repo (`172f4c52`,
`8ea41967`, `0eaa4955` there). So either it is not firing, or it is not enough.

**3. Smooth turn leaves the weapon behind entirely.** "If you hold the turn the
gun stays in place, where you can 360 degree turn and you'll spin past the gun
that's just in the air." Not a one-frame lag. The older repo's `0eaa4955`
deliberately disabled the snap correction under smooth turn, calling Team Beef's
untouched behaviour correct for it; this says otherwise. RazeXR hit the same
family (`3185cc073`): the weapon placed from the player actor's yaw while the
scene is drawn from the view's.

**Where both have got to.** The weapon's yaw is
`cl.refdef.viewangles[YAW] - hmdorientation[YAW]` (`VrInputDefault.c:195, 203`).
`cl.refdef.viewangles` is `cl.predicted_angles` (`cl_entities.c:931`), which
`CL_PredictMovement` fills by replaying the command queue — so it carries the
last command actually *sent*, not the current head pose. `snapTurn` reaches
`cl.viewangles` via `VR_GetMove` and `CL_AdjustAngles`. Lag anywhere on that
chain shows as the weapon trailing the view. **This is a chain, not a conclusion.
It has not been measured.**

**How to measure it, with no controller:** a harness driving `snapTurn` by a
fixed amount per frame reproduces a held stick, and logging

    want = snapTurn
    have = cl.refdef.viewangles[YAW] - hmdorientation[YAW]

each frame gives the angular error directly. `tools/turn-probe.py` was written
for this and applies and reverts itself. It has never run — Virtual Desktop was
down, so no session existed and `HandleInput_Default` never ran. **Ask for
Virtual Desktop to be left running and this needs nothing from him.**

### Back burner, by his own call

**4. Quest 2 only: intro, opening cutscene and first menu are double vision.**
Quest 3 is fine on the same build. Not investigated. Worth knowing before
starting: all three are `useScreenLayer()` cases, where the scene is rendered
**once** onto a quad, so ordinary stereo disagreement should be impossible and
this is not the defect-A family. `Quest_GetScreenRes` returns `cylinderSize`
rather than the eye buffer size on that path — the one thing that differs there,
and the place to look first.

### Not started

- **Six weapons need tuning by eye**: Ionripper and Phalanx (The Reckoning);
  Disruptor, ETF Rifle, Plasma Beam, Chainfist (Ground Zero). The Prox Launcher
  does **not** — `v_plaunch` is `v_launch` reskinned (same 208 verts, 384 tris,
  66 frames, byte-identical vertex data in all 66), so it takes the Grenade
  Launcher's tuned value. **Team Beef's offsets cannot be derived from geometry**
  — 7 of their 11 sit at the engine default and the variation is almost all in
  the left/right term. Do not try to fit a model to them again.
- **Not published.** No `origin` remote, only `upstream` yquake2. The Quake port
  shipped to github.com/GameOrDie007/Quake-PCVR on 2026-08-29; this one never
  did.
- **The release zip on disk is stale.** `E:\Games\Quake2VR-vr-741-base-0717e03a.zip`
  predates the ship-audit commit and is missing LICENSE. Rebuild with
  `tools/make-dist.py` from HEAD before handing anything out.

## Traps in this repo

**A `wait` chain in an exec'd cfg cannot time anything against the attract loop.**
It occupies the same command buffer the engine queues its startup `d1` commands
into, so the demo never starts and the console stays up over it.

**Screenshot comparison cannot prove a change is inert by equality.** Two runs
with identical command lines are byte-identical, which makes the method look
sound — but `0` versus `0.0` for the same cvar gives different images, because
the command-line string shifts the animation phase by a frame. Compare with a
threshold and a sensitivity control, or instrument with a probe and read the
value.

**The Bash tool mangles content two ways, and has done so three times here.**
Heredocs eat backslashes — a `\n` became a literal newline inside a C string
literal twice. And backticks inside a `python -c "..."` are command-substituted
by the shell before Python sees them, which silently deleted every identifier
from two sections of this file. **Write patch scripts and prose with the Write
tool.** The Bash working directory also persists and drifts after a `cd`, which
once turned a `ninja -C build-mingw` into a no-op; use absolute paths.

**A quit used to leave a `yquake2.exe` behind** that Task Manager would not show
and that locked the exe against the next link. Fixed in `dc8d8759`. To re-check
after any change near session teardown:

```
Get-CimInstance Win32_Process -Filter "Name='yquake2.exe'" | Where-Object { $_.CreationDate -gt (Get-Date).AddMinutes(-10) } | Select ProcessId,CreationDate
```

Nothing listed is the pass. Filter by time — strays from before the fix survive
until a reboot and will otherwise confuse the reading. If it does come back, the
workaround is to rename the exe out of the way and link again; Windows renames a
running image happily, and `Stop-Process -Force` will not shift one, because the
process has already exited.

**Beware "there is no gameplay to lose".** That reasoning, written when the menu
input branch was widened to cover cinematics, is what broke the cutscene skip —
the skip is not gameplay but it rides the gameplay usercmd. Before rerouting
input for a mode, list what else reads the usercmd in that mode.

## Why menus were the hard part

Team Beef key several behaviours off `cls.key_dest` and they all fire together
when a menu opens. **The general form, and the answers, are in the
`vr-port-diagnostics` skill** under "Keeping the world when a menu opens" and
"The attract demo is a world, not a film". Read that rather than re-deriving it,
and add to it if this port turns up anything new.

## How to resume

1. Read this file, then `git log --oneline -15`.
2. The tree should be clean.
3. He is an expert headset tester and not a programmer — give him commands to run
   and things to look at, never code to read. Headset rounds are the scarce
   resource: verify everything possible at the desk first, and see above for how
   much of it the desk can actually do.

## Done

Newest first. `git show <hash>` for the reasoning; each message carries it.

| commit | what |
|---|---|
| `4db08e25` | Cutscene skip restored — two earlier fixes had cancelled each other out |
| `77a80fe5` | The demo is a real world; the head steers it instead of wearing it |
| `50d70c8f` | The first menu keeps the world behind it |
| `d72929a5` | HUD scale (`xh` in raw pixels against `scale`); every button works in a movie |
| `dc8d8759` | OpenXR session destroyed on quit, so the process can be reaped |
| `c7cf0894` | Any deliberate press skips a cinematic, not just the trigger |
| `4823d8df` | The menu on a composition layer of its own — `vr_menu_in_world 2` |
| `6f30b37e` | PAUSED and centerprints sit in the menu's plane, not the HUD's |
| `becbece0` | A pause menu that fuses, over a world that stays lit |
| `09b26186` | Plasma Beam from the gun; weapon alignment tuner; pause without leaving VR |
| `46e25171` | The release build refuses to ship game data |

**Confirmed in the headset:** the pause menu fuses; 3.5m is the right distance
("nice and big and easy to read"); the world stays lit; PAUSED sits correctly;
the menu stays put when fixed and follows the gaze when not; the process leak is
gone; and the first menu's demo is in the world, with the menu pausing and
navigating over it.
