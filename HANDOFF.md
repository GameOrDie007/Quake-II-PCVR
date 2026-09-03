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

Defects A and B from the previous handoff; C, which the headset found once A and
B were in; and D, the gaze-lock, which the owner asked to have fixed properly
rather than cheaply. **All four are confirmed working in the headset.**
Everything is still behind `vr_menu_in_world`, which defaults to 0.

The same session turned up four new things, none of them to do with the menu,
and the owner then reported the quit that leaves a process behind and an
overlapping HUD. See "Found in the headset" below. **The process leak is fixed
and confirmed**; the HUD overlap and the input routing are fixed and
desk-verified; the two turn bugs are diagnosed but unmeasured; the Quest 2
double vision is back burner.

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

### C. PAUSED and centerprints follow the menu's plane

Reported from the headset after A and B were in. `PAUSED` was not unfused - it
was fused at `vr_hud_depth` (0.5m) while the eyes were converged on the menu at
`vr_screen_depth` (3.5m), so it doubled. `SCR_GetStereoOverlayOffset` returns
the menu's offset when `VR_MenuInWorld()` and the HUD's otherwise;
`SCR_DrawPause` and `SCR_DrawCenterString` both use it. The centerprint was not
reported - same defect one line away, and a level hint can still be on screen
when a menu opens.

The HUD proper deliberately stays at `vr_hud_depth`.

### D. The menu no longer rides the head

`vr_menu_in_world` is three-way now: 0 off (Team Beef's), 1 in the eye buffers
and following the head, 2 on a composition layer of its own and staying where it
was opened. The PC Options item says "no / yes, follows gaze / yes, fixed in
place".

At 2 the eye pass skips `M_Draw()` and `SCR_DrawMenuLayer()` draws it once into
a third swapchain, cleared transparent, submitted as a quad after the projection
layer. PAUSED is on the layer too, so C cannot come back. Nothing on the layer
takes a per-eye offset - the compositor is what makes a quad stereo.

The quad is sized from the field of view so the menu keeps the size it already
has, and placed at `vr_screen_depth`. If the swapchain cannot be created,
`VR_MenuOwnLayer()` says so and the menu falls back to being drawn in the eye
buffers, as at 1.

### What A does not cover

- `R_RenderFrame` at `menu.c:5251`, the spinning player model on Multiplayer ->
  Player Setup. An x shift would move the viewport and not the model in it.
- `M_Popup()` + `R_EndFrame()` at `menu.c:1421` and `:3526` - the sound-restart
  and server-search messages force a buffer swap from inside a key handler,
  outside the bracket. Already hostile to a stereo path before this change.

### How it was verified without a headset

**Read this before assuming you need the owner.** `VirtualDesktopXR` is
installed on this machine, and while the owner has Virtual Desktop running a
second process gets a **real OpenXR session of its own**. The whole VR path runs
at the desk. Check for it with:

```
grep -a "OpenXR runtime is\|session created\|creating swapchain" build-mingw/release/stdout.txt
```

Three `creating swapchain` lines means the menu layer's swapchain was made too.
An earlier version of this file said the opposite - that `TBXR_IsRunning()` is
false with no headset, so nothing could be tested. That was wrong, and it
under-used the desk badly.

Runs are driven with a generated config - `map base1`, 200 `wait`s, `menu_main`,
`screenshot`, `quit` - executed with `+exec dbgshot.cfg`.

- A temporary forced offset of +40px moved the plaque, the logo, the cursor, all
  five menu items and the highlight, and moved neither the world nor the HUD.
  That is the proof that nothing bypasses the choke point.
- A temporary `Com_Printf` probe read the offset each frame: 0 with the menu up,
  the HUD's own +-507 during play.
- With the feature off the menu is unchanged, measured: the menu band differs
  from the pre-change build by at most 24 out of 765, where a 1px shift of the
  same image gives 163. PAUSED cross-correlates to a minimum at +0 px.
- The fade still fires with the feature off: world luminance 3.60 against 18.07.
- The three modes were run one after another and screenshotted. 0 has the menu
  centred over a dimmed world, 1 has it offset per eye over a lit world, 2 has
  no menu in the eye buffer at all. Three runs, three different pictures, each
  different in the right way.
- The menu layer's own drawing was dumped with its alpha: correct layout on a
  background 95.1% fully transparent.
- The quad's geometry was logged: `7.754 x 8.378 m at 3.50 m` - 96 x 100 degrees,
  the eye buffer's own field of view - at a pose 3.500m from the head, yaw only.
- A full run submitting two composition layers produced no `XR_ERROR`.

## What the headset has already said

Tested 2026-09-03 in `E:\Games\Quake II VR`. `vr_menu_in_world` is
`CVAR_ARCHIVE` and is already `1` in his `baseq2/config.cfg`, so the ordinary
launcher enables it; `vr_weapon_tune` is registered with no archive flag and
cannot persist, which is the whole of defect C from the previous handoff.

First session:

- **The menu fuses.** A is fixed.
- **3.5m is the right distance** - "nice and big and easy to read".
- **The world is bright and the menus are readable.** B is fixed.
- **PAUSED did not fuse.** Led to C.
- **The menu is attached to his gaze.** Led to D.

Second session, on the build with C and D in - **all four confirmed**:

> "Everything works the way it should. Paused does not feel like it's just an
> underlayer of the main menu, no double vision or anything. It follows gaze
> when that is enabled, and when it's fixed in place, it's fixed in place."

So A, B, C and D are done. What he found instead is four new things, below.

## Found in the headset 2026-09-03, not yet fixed

### 7. The startup menu in the world (built, NOT verified)

The owner asked whether the first menu - the one over the attract demo, after
the id movie - could be in the world like the pause menu. It can, and it is one
condition: the attract demo is served by a **local server in attract mode**, so
the client sits at `ca_active` with a real world to draw. Team Beef sent it to
the flat quad along with everything else that is not gameplay.

`VR_InWorldEligible()` now carries the shared test - feature on, session running,
`ca_active`, no cinematic - and `VR_MenuInWorld()` is that plus `key_dest ==
key_menu`. `useScreenLayer()` is deliberately **wider**: it keeps the demo in the
projection layer whether or not the menu is open, so that opening the menu does
not flip the whole scene between a flat quad and stereo. The console is excluded
and stays on the flat panel.

**He was warned and chose it anyway.** The demo is a recorded fly-through, and a
camera that moves without the head is the usual way to make someone ill. He
picked "keep the demo" knowing that. If it is unpleasant, the whole thing backs
out by putting `cl.attractloop` back into `VR_InWorldEligible()`.

**Not covered:** the no-connection case. Skip the id movie and no demo plays -
`cls.state` is `ca_disconnected`, there is no world at all, and the menu stays a
flat panel. Making that one in-world needs the projection layer told to render a
deliberately empty scene, which is real work and was not part of this.

**Not verified, and it is worth knowing why.** Four desk runs failed to check it,
each for a different reason, and the last one exposed something worth recording:
**a session that is created but never reaches running freezes the game.**
`q2xr_Frame` returns early while `gApp.SessionRunning` is false, and it is
`q2xr_Frame` that calls `Qcommon_Frame` - so with Virtual Desktop up but the
headset not streaming, nothing ticks at all. No map loads, no frame renders. That
is also the state a player would land in if they launched with VD running and the
headset asleep, so it is worth handling on its own account.

### 8. The demo is a world, and the head now steers it (fixed, desk-verified)

The owner tried 7 in the headset: "it feels like a movie attached to my head, so
it feels weird", and asked whether the demo could be unlocked so he could look
around, or else be reverted.

**It is a world, not a film.** The attract loop is demo playback of real BSP
geometry - the log says  - rendered live every frame. The id logo at
startup is a genuine cinematic; this is not.

**The recording owned the view completely.** Measured with a probe rather than
argued:  is **4, PM_FREEZE**, so  takes the
interpolated branch and  follows the playerstate degree for
degree, while  sits at 0.0 throughout. Local input had no say.

That is exactly what made it ride the head: the scene is drawn facing wherever
the recording faces, then submitted on a projection layer posed at the head, so
the compositor presents it as though it had been drawn facing where the head is.

The fix is four lines in : while , the head owns all three angles and the recording keeps
only the position. Carried along its path, free to look anywhere. Pitch and roll
come from the head too - an imposed horizon is worse than an imposed yaw.
 lost its  and is now in .

**Proof at the desk, no headset motion needed:** with the headset parked so
 is constant, the probe showed  walking -180.0 ->
-168.1 while  held at -88.1, matching  exactly. Before the change
 tracked . That is the switch.

**A screenshot of it could not be taken, and the reason is worth keeping:** a
 chain in an exec'd cfg occupies the same command buffer that Quake II
queues the startup  attract-loop commands into, so the demo never starts in
a cfg-driven run and the console stays up over it. Any future attempt to
photograph the attract loop needs a different lever than .

Backing the whole demo-in-world thing out is still one edit: put 
back into .

### 6. The HUD drew health and ammo on top of their own icons (fixed, desk-verified)

`single_statusbar` (`g_spawn.c:725`) positions with **`xh`**, which is Team
Beef's own layout command - it is in neither stock Quake II nor yquake2, and
arrived with the vendor commit. Theirs placed it in **raw pixels**, while the
digits and icons it positions are drawn at `scale`. That holds together only
while `scale` is about 1. On PC it is around five, so the three health digits
were drawn some 250px wide into a slot 50px from their icon, and the icon landed
on top of them. Ammo the same.

`xh` now uses `xv`'s expression. The offsets in the layout - 0, 50, 100, 150,
200, 250, 296 - are a stock 320-wide layout, which is what `xv` is for, so this
is what they have always meant; and at `scale` 1 it is byte-for-byte theirs.

Desk-verified by photograph: before, the cross sat over the digits; after, "100"
and the cross sit side by side, correctly spaced. This is the
[[vr-port-platform-seam-pattern]] exactly - their code is fine on their
platform, and the PC's larger UI scale is what breaks it.

### 1. Cinematics, the demo loop, and the menu ignored A and the trigger (fixed, untested)

Three symptoms, reported across two rounds, all one cause:

- A did not skip the startup id movie.
- A did not skip the opening cutscene.
- At the demo loop that plays behind the first menu, only B brought the menu
  up - not A, not the trigger.

**Only B produces a `Key_Event`.** In gameplay Team Beef send the trigger out as
a `+attack` console command and A as `+movedown` (`VrInputDefault.c:386, 394`),
and a console command never reaches `Key_Event`. B alone goes through
`handleTrackedControllerButton(..., K_SPACE)`. And `Key_Event` is exactly what
breaks the attract loop into the menu - `cl_keyboard.c:1174` turns any key into
`K_ESCAPE` there - and what dismisses the startup movie, which has no connection
and so never reaches the skip in `CL_SendCmd` at all.

The trigger *appeared* to work on an in-game cutscene only because `+attack`
sets `BUTTON_ATTACK`, which `CL_SendCmd` does check - and that path needs a
connection.

Two changes:

- `HandleInput_Default`'s menu branch now also covers `cl.attractloop` and
  `cl.cinematictime > 0`, so during a movie or the demo every button is a key
  event, as it already was in a menu. Losing gameplay input there costs nothing,
  because there is no gameplay to lose.
- `CL_SendCmd`'s in-game skip counts `cmd->upmove` as well as `cmd->buttons`,
  which is what made A skip the opening cutscene in the first round. `upmove` is
  driven only by the in_up/in_down key states, never by head or room-scale
  movement, so it cannot fire on its own, and the one-second guard still applies.

The first round's fix was made without this second half, and the note here said
at the time that it did not explain why B worked and A did not. It now does.

### 5. Quitting left the process behind and locked the exe (fixed, unconfirmed)

Reported by the owner: "we need to fix this Quake II bug where the .exe is being
held open, because it doesn't show up in task manager at all". It had been
costing this session a rename-the-exe dance on nearly every build.

**`TBXR_ShutdownOpenXR()` was never called from anywhere.** Every VR quit left
the session and the instance alive. Two things follow, and they explain both
halves of what he saw:

- The process really does exit - `HasExited` is true on every one of them - so
  Task Manager's process list is right to not show it. But it is never
  **reaped**: the runtime still holds a handle, so the kernel object survives
  with its image section mapped, and that is what keeps `yquake2.exe` locked.
- `q2xr_DestroyOpenXR` would not have helped even if it had been called. It went
  straight to `xrDestroySession`, which on a **running** session returns
  `XR_ERROR_SESSION_RUNNING` and destroys nothing.

So both halves are fixed. `q2xr_EndSessionAndWait` does what the spec asks -
`xrRequestExitSession`, then pump events until the runtime drives the session
through `STOPPING`, which the existing event handler already turns into
`xrEndSession` - and `CL_Shutdown` now calls `TBXR_ShutdownOpenXR()` before
`VID_Shutdown()`, which is the right side of it because the swapchain images are
textures in the GL context `VID_Shutdown` destroys. The wait is bounded at two
seconds: trading a stray process for a hung quit would be worse.

**Confirmed fixed.** The owner ran the fixed build in the headset at 14:36 -
`stdout.txt` in the install shows `session created` and three swapchains, so it
was a real session - quit, and **left no process behind**. No "session did not
stop in time" either, so the teardown completed inside the bounded wait. Before
the fix, every session-bearing run left one.

To check it again after any change here:

```
Get-CimInstance Win32_Process -Filter "Name='yquake2.exe'" | Where-Object { $_.CreationDate -gt (Get-Date).AddMinutes(-10) } | Select ProcessId,CreationDate
```

Nothing listed is the pass. Filter by time - strays from before the fix persist
until a reboot and will otherwise confuse the reading.

### 2. Snap turn throws the weapon to one side for a frame

"The controller stays on one side of the screen for a split second and then
appears in its proper place."

The correction for exactly this is **already in the tree** -
`VrInputDefault.c`, `snapTurnAtEntry` at the top and the block at the end,
gated on `vr_smoothturn == 0`. It was ported from the older `Quake2VR` repo
(commits `172f4c52`, `8ea41967`, `0eaa4955` there). So either it is not firing,
or it is not sufficient here.

### 3. Smooth turn leaves the weapon behind entirely

"If you hold the turn, either left or right, the gun stays in place, where you
can 360 degree turn and you'll spin past the gun that's just in the air."

That is not a one-frame lag, it is the weapon not following at all. Note the
older repo's `0eaa4955` deliberately **disabled** the snap correction under
smooth turn, calling Team Beef's untouched behaviour "correct for it". This
report says it is not. RazeXR hit the same family - `3185cc073`, "the weapon was
placed from the player actor's yaw while the scene is drawn from the view's, and
those deliberately disagree while turning".

**Where 2 and 3 have got to.** The weapon's yaw comes from
`cl.refdef.viewangles[YAW] - hmdorientation[YAW]` (`VrInputDefault.c:195, 203`).
`cl.refdef.viewangles` is `cl.predicted_angles` (`cl_entities.c:931`), which
`CL_PredictMovement` fills by replaying the **command queue** - so it carries the
last command actually sent, not the current head pose. `snapTurn` reaches
`cl.viewangles` through `VR_GetMove` and `CL_AdjustAngles`. A lag anywhere along
that chain shows up as the weapon trailing the view, which is what both reports
describe. **This is a chain, not a conclusion - it has not been measured.**

**How to measure it** - see the OpenXR note above; this needs no controller. A
harness that drives `snapTurn` by a fixed amount per frame reproduces a held
stick, and logging

    want = snapTurn
    have = cl.refdef.viewangles[YAW] - hmdorientation[YAW]

each frame gives the weapon's angular error directly. The harness was written
this session (`tools/turn-probe.py`, applies and reverts itself) but could
not be run: Virtual
Desktop was not up, so no session was created and `HandleInput_Default` never
ran. **Ask for Virtual Desktop to be left running and this is a desk job.**

### 4. Quest 2 only: intro, opening cutscene and the first menu are double vision

Quest 3 is fine, same build. He called it back burner.

Not investigated. Worth knowing before starting: all three of those are
`useScreenLayer()` cases, where the scene is rendered **once** and shown to both
eyes on a quad - so ordinary stereo disagreement should be impossible, and
whatever this is, it is not the defect A family. `Quest_GetScreenRes` returns
`cylinderSize` rather than the eye buffer size on that path, which is the one
thing that differs there and is worth looking at first.

## What a headset session should check next

1. **The startup menu in the world.** Item 7 - built and never once run. Does
   the demo behind the first menu render in stereo, does the menu hang in place,
   and - the reason he was warned - is the demo's own camera motion bearable? If
   not, that is one condition to back out.
2. **Does A now skip the id movie and the opening cutscene, and does any button
   bring up the first menu?** Item 1 above.
3. **Is the HUD still readable and sensibly placed?** Item 6 moved it; the
   overlap is gone but the spacing has only been judged at the desk.
3. Then the things that have still never run in a headset, below - the game
   select page and `relaunchgame` first, since that is the riskiest.

Items 2 and 3 above should be **measured at the desk before he is asked
anything**, which needs Virtual Desktop left running. Item 4 is back burner by
his own call.

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
- **The menu layer has never been seen in a headset.** Everything about it that
  can be measured has been; what it looks like has not.
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

**A quit used to leave a `yquake2.exe` behind** that Task Manager would not show
and that locked the exe against the next link, failing it with
`cannot open output file release\yquake2.exe: Permission denied`. That was the
never-called `TBXR_ShutdownOpenXR` - see item 5 above - and should now be gone.
If it comes back, the workaround is to rename the exe out of the way and link
again; Windows will rename a running image quite happily. `Stop-Process -Force`
will not shift one of these, because the process has already exited.

**Screenshot comparison cannot prove flatscreen is unchanged by equality.** Two
runs with identical command lines are byte-identical, which makes the method
look sound - but passing `0` versus `0.0` for the same cvar produces different
images, because the command-line string shifts the animation phase by a frame.
Compare with a threshold and a sensitivity control (shift the same image by one
pixel and measure that), or instrument with a `Com_Printf` probe and read the
value. See [[vr-port-dump-the-buffer]] and [[vr-port-verify-before-asking]] in
the owner's memory.

**The Bash tool's heredocs eat backslashes**, which corrupted a `\n` into a
literal newline inside a C string literal twice in the previous session, and
again in this one - in the very script written to record that it does. Write
patch scripts with the Write tool, not a heredoc. The Bash tool's working
directory also persists across calls and drifts after a `cd`, which silently
turned one `ninja -C build-mingw` into a no-op; use absolute paths.

## How to resume

1. Read this file, then `git log --oneline -15`.
2. The working tree should be clean at the commit that added this file.
3. The owner is an expert headset tester and not a programmer - give him
   commands to run and things to look at, never code to read. Headset sessions
   are the scarce resource; verify everything possible at the desk first.
