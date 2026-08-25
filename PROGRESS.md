# Progress

---

# REBASE STATUS — updated 2026-08-24

Branch `vr-741-base` in the `Quake2VR-741` worktree. Three commits so far.

## Done

**1. Stock 7.41 builds on Windows.** MSVC cannot compile it - 45 C99
variable-length arrays across seven renderer files, plus `__attribute__` and
`strcasecmp` - and editing engine source to satisfy it is exactly what this
rebase exists to avoid. MinGW GCC 16.1.0 from MSYS2 compiles it untouched.
Build-file changes only: `cmake_minimum_required` 3.0 -> 3.5, lower-case the
ARCH string, guard the GCC-only flags, and `-fcommon` (GCC 10 flipped to
`-fno-common`, which turns tentative definitions like `cvar_t *m_yaw;` in two
translation units into a duplicate-symbol link error). Verified by running
`base1` to a connected client.

**2. Their engine diff applied with zero conflicts.** All 62 files, 6,936 diff
lines, +2,746/-870, not one rejected hunk. This is the whole point of the
rebase: the same diff could not be applied to 8.71pre, which is why it was
previously hand-picked and approximated, and why every weapon, height and
movement bug traced back to that.

One trap worth recording: the diff was generated between an LF tree and their
CRLF tree, so it carries mixed line endings and initially failed *every* hunk
with "different line endings". Nothing was wrong with the content. The
worktree is now `core.autocrlf false` / `core.eol lf`, matching what the git
blobs already stored, and the diff is normalised to LF before applying.

**3. Their changes compile.** Six platform-seam gaps, none in the VR logic:
include paths into their Android layout; `u_int32_t` (its absence made
`VrCommon.h` fail to parse, so every `handleTrackedControllerButton` call
looked undeclared - one typedef cleared five errors); the GLES float spellings
`glFrustumf`/`glOrthof`/`glDepthRangef`, now mapped to the desktop double forms
in `qgl.h`; `hmdType`, which they declared inside `#ifdef __ANDROID__` but pass
to `re.Init` unconditionally; `cl_maxfps`, which they deleted but still
reference in the dedicated-server half of `frame.c` that Android never builds;
and gl3, which their tree never made compile.

`ref_gl1.dll`, `ref_soft.dll` and `game.dll` build.

## Next: port `Q2VR_SurfaceView.c`

The client does not link yet. Everything still missing - `VR_Init`,
`VR_GetMove`, `getVROrigins`, `getFOV`, `TBXR_UpdateControllers`,
`QuatToYawPitchRoll`, `Android_Vibrate`, and the entire `vr_*` cvar set with
their tuned defaults - lives in that one 1,659-line file, their platform layer.

Roughly lines 181-1468 are portable OpenXR work and should be ported close to
verbatim: action setup and controller bindings (489-656), controller polling
(657-726), haptics (727-773), instance and session creation (784-1029), the eye
loop (1030-1220), `VR_Init` with the cvar defaults (1328-1367), and the pose
maths and origin accessors (1368-1468).

Lines 1469-1659 are Android lifecycle, the app thread and JNI, which the
existing Windows backend replaces. The EGL context (261-369) becomes the
WGL/SDL context, the GLES framebuffer helpers (370-488) become desktop GL, and
`Quest_GetScreenRes`/`Quest_GetRefresh`/`Quest_MessageBox` get PC equivalents.

Port their structure rather than substituting the OpenXR layer already written
on the `vr` branch - that layer was built to a different design and its
constants were re-derived rather than carried over, which is the mistake this
rebase is correcting.

## Notes

- gl3 is `option(GL3_SUPPORT ... OFF)`. Their VR work is in **gl1**; the first
  attempt built the VR path on gl3, a renderer they never touched.
- The old `vr` branch keeps the previous attempt. Divergences to leave behind:
  `vr_seated`, `vr_hud_scale`, `vr_turn_speed`, `vr_roomscale`, `vr_smoothturn`
  as a mode switch, the scissor comfort aperture, the beam laser sight, the
  zeroed `vr_weapon_adjustment` default and `VR_RoomscaleOffset`.
- Run test builds with `-portable` so the tuned `config.cfg` under OneDrive is
  never read or rewritten.

---

# CURRENT PLAN — 7.41 rebase (read this first)

**Decided 2026-08-24. Supersedes the approach used for milestones 0–8.**

## The owner's requirement, stated plainly

An **exact 1:1 port** of Quake2Quest to PCVR. Same menus, same weapon models,
same behaviour. **Nothing diverges** from their build except what is strictly
required to run on PC. Older is fine — matching them beats being current.

Everything below exists because that requirement was not respected early on.

## Root cause of the wasted effort

`src/vr` was built on **yquake2 8.71pre**. Quake2Quest forks **7.41**. That
divergence was identified in milestone 0, written down as a risk, and then
built on anyway. Their 62-file engine diff cannot land cleanly across four
years of upstream drift, so it got hand-picked and approximated instead —
which is where every weapon, height and movement bug came from.

## The plan

**Rebase onto yquake2 7.41**, the exact version they forked, then apply their
diff.

Already done:

- Worktree at `E:\Tools\Games\Quake2VR-741`, branch `vr-741-base`, at commit
  `8bcb8f84` ("Bump version number to 7.41")
- Their complete engine diff extracted to
  `E:\Tools\Games\Quake2VR-refs\tb-engine.diff` — **62 files, 6936 lines**,
  produced by diffing stock 7.41 against their vendored `quake2/src`
- Stock 7.41 extracted for diffing at `E:\Tools\Games\Quake2VR-refs\yq2-741`

Remaining:

1. Reconcile the build. 7.41's own CMakeLists has **no MSVC support**, so use
   8.71's build plumbing over 7.41 sources. Known gaps: `cl_image.c`,
   `gl1_buffer.c`, `refresh/files` split, `miniz.c` path (directly under
   `unzip/`, not `unzip/miniz/`), `savegame/tables/entfields.h`; and 7.41 *has*
   `gl1_md2.c` / `gl1_sp2.c` which 8.71 dropped. Iterate against CMake's errors.
2. Build stock 7.41 under MSVC as a baseline before touching anything.
3. Apply their engine diff, skipping Android-only files
   (`backends/unix/main.c`, `signalhandler.c`, `system.c`).
4. Bring across the already-ported VR layer in `src/vr/teambeef/` — that part
   worked and needs no redoing.
5. Bring across the PCVR platform half (below).
6. **Remove every divergence of ours** — see the list further down.

## Their renderer is gl1, not gl3

Their renderer changes: **gl1** 190 + 152 + 52 + 32 lines; **gl3** 85 + 35 + 26
+ 25. `gl1_mesh.c` alone is 152 lines, and that is where weapon model drawing
lives.

This port built its entire VR render path on **gl3**. For 1:1 the VR path must
move to **gl1**, where their work is. This is plausibly why the weapon fought
back for so long: their weapon logic was being fitted onto a renderer they
never modified.

## What stays ours, and only this

The PCVR platform half, which has no counterpart in their build because they
target Android:

- OpenXR instance / session / swapchains / spaces (`vr_instance.c`,
  `vr_session.c`, `vr_swapchains.c`, `vr_spaces.c`)
- The OpenGL binding via `XrGraphicsBindingOpenGLWin32KHR`
- The eye-render loop driven from `SCR_UpdateScreen`
- The desktop mirror blit
- Flatscreen fallback and the `vr_enabled` gate
- CMake build plumbing

**This is the reusable part.** It transfers to Raze (agreed as the next
project) and to Prey.

## Divergences of ours to remove

Not theirs, and to be deleted unless the owner asks otherwise:

`vr_seated`, `vr_hud_scale`, `vr_turn_speed`, `vr_roomscale`, `vr_smoothturn`
as a mode switch, the scissor-based comfort aperture, the beam laser sight, the
zeroed `vr_weapon_adjustment` default, and `VR_RoomscaleOffset`.

Smooth turning is available in their own VR options menu — the owner played it
that way — so porting `menu.c` (415 lines) restores it natively.

## The workflow, for Raze and Prey

1. Find which upstream version the VR fork was made from. Get exactly that.
2. `diff -ru <stock base> <VR fork>` — the complete changeset, nothing filtered
   by judgement.
3. Classify: VR layer (drops in), engine integration (apply), platform
   (replace), build system (replace).
4. **Build on their base version, not the newest.** This is the step that was
   skipped here and it cost a day.
5. Replace only the platform seam.

Next project after Quake II: **Raze**. Raze already builds natively on Windows
and RazeXR forked Raze 1.7, so pinning to 1.7 avoids this problem entirely.
Note the public **VRaze** repo is README-only — no source — so VRaze's own
features need a source request to its author.

---


Updated at the end of every session. See `PLAN.md` for architecture,
`RUNTIME_NOTES.md` for OpenXR runtime quirks.

---

## Session 1 — 2026-08-23

### Milestone 0 (survey) — complete, awaiting review

**Works**

- All three reference repos cloned to `E:\Tools\Games\Quake2VR-refs\`:
  `yquake2` (8.71pre), `Quake2Quest`, `q3vr` (v1.0).
- This repo is a full-history clone of yquake2 with `upstream` pointing at
  `github.com/yquake2/yquake2`, so we can rebase on upstream later.
- `xrprobe` built and run against both runtimes —
  `../Quake2VR-refs/xrprobe/` (probe.c, build.bat, xrprobe.exe).
- `PLAN.md`, `RUNTIME_NOTES.md`, `PROGRESS.md` written.

**Decided**

- **Graphics binding: OpenGL**, via `XrGraphicsBindingOpenGLWin32KHR`. Both
  VDXR (v10) and SteamVR (v12) advertise `XR_KHR_opengl_enable`. The
  `WGL_NV_DX_interop2` fallback is dropped; SteamVR is not required.
- **Request OpenXR `apiVersion` 1.0**, not the SDK's 1.1 — both runtimes reject
  1.1 (see RUNTIME_NOTES.md, cause not fully pinned down).
- **Hand-port Quake2Quest's VR hooks; do not merge its engine tree.** It vendors
  yquake2 7.41 against upstream's 8.71pre.

**Measured with the Quest 3 connected and streaming**

- Recommended per-eye **3072 × 3264**, 1 sample. Max layers 16. `OPAQUE` blend
  only.
- **GL 4.0–5.0 required by VDXR.** Yamagi's `gl3` asks for 3.2 core at
  `src/client/refresh/gl3/gl3_sdl.c:253-255` — **VDXR will refuse it.** Raising
  this via a 4.6-down fallback ladder is now a known milestone-2 task.

**Decided by owner (2026-08-23)**

- Renderer boundary: **(a) extend `refexport_t`**, (b) static-link `ref_gl3`
  held in reserve.
- Hand-port Quake2Quest hooks rather than merge: accepted.
- Milestone order unchanged.

**Broken / unknown**

- Nothing is broken — no engine code written yet.
- The apiVersion 1.1 rejection on SteamVR is still unexplained; suspected to be
  the borrowed loader binary. Not blocking, since we request 1.0.
- sRGB swapchain requirement is carried over from Forsaken but unconfirmed here.
  First thing to suspect if milestone 2's solid colour looks washed out.

---

## Milestone 1 (flatscreen baseline) — build done, play-test pending

**Works**

- **Unmodified Yamagi builds clean with MSVC**, zero errors. VS Build Tools
  2026 (MSVC 14.51 / cl 19.51), CMake 4.3.1, generator `Visual Studio 18 2026`.
  Recipe written up in `BUILD-WINDOWS.md`.
- Dependencies from `dhewm3-libs` as upstream CI does; the bundle ships MSVC
  `.lib` files, so an MSVC build links against its `-w64-mingw32` tree fine.
- All targets produced in `build/release/RelWithDebInfo/`: `yquake2.exe`,
  `quake2.exe`, `q2ded.exe`, `ref_gl1/gl3/gles3/soft.dll`, `baseq2/game.dll`.
  Runtime DLLs (SDL2, OpenAL32, curl) staged alongside.
- **Engine smoke-tested without game data**: `q2ded.exe` reaches
  `==== Yamagi Quake II Initialized ====` and waits for a map. The engine core
  runs.

**Game data complete and verified**

Game data lives at **`E:\Games\Quake 2`** (copied from the owner's Gaming PC,
2026-08-23). Verified against the MD5s published in `doc/020_installation.md`:

| file | status |
|---|---|
| `baseq2/pak0.pak` | MD5 match (183,997,730 bytes) |
| `baseq2/pak1.pak` | MD5 match |
| `baseq2/pak2.pak` | MD5 match |
| `ctf/pak0.pak` | MD5 match |
| `rogue/pak0.pak` | MD5 match |
| `xatrix/pak0.pak` | MD5 match |
| `baseq2/video/` | present, 11 `.cin` files |

All six paks are genuine correct-vintage retail data. `pak0.pak` arrived on a
second copy from the Gaming PC.

Two files that look like candidates and are **not**, recorded so nobody tries
them later:

- `E:\Games\Quake 2\rerelease\baseq2\pak0.pak` (1.7 GB) is the 2023 remaster's
  KEX-engine data. Wrong assets for Yamagi.
- `Quake2Quest/assets/pak0.pak` (47.6 MB, md5 `27d77240466ec4f3253256832b54db8a`)
  is **not** the retail pak0 either — the MD5 does not match.

**Data loading verified headlessly.** `q2ded.exe -datadir "E:\Games\Quake 2"
+map base1` reaches `50 entities inhibited. 1 teams with 2 entities.` — paks
read, BSP loaded, entities spawned, game DLL running. See `BUILD-WINDOWS.md`
for the exact command; it is the fastest build-vs-VR-path triage we have.

**Remaining for milestone 1:** owner launches the GL client and confirms it
plays on the monitor. Using `-datadir`; paks are never copied into the tree.

**Environment gotcha found:** this machine sets
`NoDefaultCurrentDirectoryInExePath`, so `cmd.exe` will not run an executable
from its own working directory. Batch files must call exes by full path
(`"%~dp0q2ded.exe"`). Cost time three separate times before being identified,
because the error claims the file does not exist. Written up in
`BUILD-WINDOWS.md`.

**Environment note for milestone 8 (config persistence)**

Yamagi's write directory on this machine is **OneDrive-redirected**:
`C:\Users\Miles\OneDrive\YamagiQ2\` holds `config.cfg`, `console_history.txt`
and the logs. Our `vr_*` cvars will persist there, not to a local path. Worth
remembering when config changes appear not to stick — OneDrive sync conflicts
are a plausible cause that would otherwise look like a cvar-archiving bug.

**Notes**

- `SDL3_SUPPORT` defaults ON in `CMakeLists.txt`; must be passed `OFF`.
- Upstream calls `CMakeLists.txt` unmaintained and prefers the Makefile. The
  Makefile is MinGW-only so it is not an option for us. Expect friction on
  upstream rebases when new sources are added to the Makefile but not to CMake.

**Milestone 1 closed** — owner confirmed the flatscreen client plays.

---

## Milestone 2 (OpenXR bring-up) — built, awaiting headset test

**Works (verified by me)**

- **Builds clean** with VR support, zero errors. The OpenXR loader is fetched
  and built from source by CMake `FetchContent`, pinned to `release-1.1.62`,
  and copied next to `yquake2.exe` by a post-build step.
- **Flatscreen is not regressed.** With `vr_enabled 0` the client still loads
  `ref_gl3.dll` and gets a GL **3.2** context — byte-identical behaviour to
  upstream — despite the `API_VERSION` bump and the new context ladder.
- CI workflow added: `.github/workflows/vr_win64.yml`. Builds x64 with VR,
  asserts `openxr_loader.dll` was staged, smoke-tests `q2ded`, and uploads a
  Windows artifact plus debug symbols.

- **Full OpenXR bring-up confirmed on hardware**, headset connected and
  streaming. Complete chain, no warnings, session held for 25s:

```
GL3_InitContext(): got an OpenGL 4.6 core context.
[VR] runtime: VirtualDesktopXR 1.0.10
[VR] system: Meta Quest 3 (orientation yes, position yes)
[VR] recommended per-eye: 3072x3264 (1 samples)
[VR] requires OpenGL 4.0 - 5.0
[VR] session created.
[VR] world space: STAGE (roomscale, floor origin)
[VR] swapchains: 3072x3264 per eye, 3 images each
[VR] OpenXR ready.
[VR] session running.
```

  Note **STAGE** space was granted — real roomscale with a floor origin, so
  milestone 5 does not need a guessed eye height.

**Not yet verified — needs the owner's eyes**

- Whether the colour actually *appears* in the Quest 3. The software path
  reports success end to end, but only a human in the headset can confirm
  pixels arrived, and confirm they are a solid colour rather than washed out.

**Fixed during this milestone: a pointer-truncation crash**

First run crashed instantly with `0xC0000005`. Cause: `vr_cvars.c` included
only `shared.h`, but `Cvar_Get` is declared in `common.h`. Implicitly declared,
C assumes `int` return, x64 truncates the pointer to 32 bits — producing a
non-NULL garbage cvar pointer that passed the `if (vr_enabled)` guard and
faulted on `->value`. MSVC only warns (C4013), so it built clean.

Full write-up, including how the RVA was resolved from the event log via
`dumpbin`, is in `BUILD-WINDOWS.md`. **Grep new builds for C4013.**

**What was built**

New `src/vr/` module, mirroring q3vr's split:

| file | role |
|---|---|
| `vr_base.c` | lifecycle, logging helpers, `VR_Active`/`VR_Requested` |
| `vr_instance.c` | instance, system, view config, GL requirements |
| `vr_session.c` | session + GL binding, session state machine |
| `vr_swapchains.c` | per-eye `GL_SRGB8_ALPHA8` swapchains |
| `vr_spaces.c` | STAGE world space, LOCAL fallback, VIEW space |
| `vr_render_loop.c` | wait/begin/locate/render/end |
| `vr_math.c` | asymmetric projection, quat→Quake angles |
| `vr_cvars.c` | `vr_enabled`, `vr_resolution_scale`, `vr_test_color`, `vr_debug` |

Engine changes, all runtime-gated:

- `ref.h`: `API_VERSION` 8→9; `refexport_t` gained `VR_GetGLContext`,
  `VR_BeginEye`, `VR_ClearEye`, `VR_EndEye` — decision (a) from PLAN.md §3.
- `gl3_vr.c`: the renderer half. Hands over `wglGetCurrentDC/Context`, renders
  eyes through a reused FBO, restores the previous target exactly.
- `gl3_sdl.c`: GL context ladder. **VR on** walks 4.6→4.5→4.3→4.1→4.0→3.2;
  **VR off** asks for 3.2 only, exactly as upstream does.
- `cl_main.c`: four guarded hooks — cvars before `VID_Init`, `VR_Init` after,
  poll+render after `SCR_UpdateScreen`, shutdown before `VID_Shutdown`.

**Design notes worth remembering**

- The VR frame runs *after* the flatscreen one, so the mirror window keeps
  working and milestone 3 has somewhere to stand.
- Once a session is live, `xrWaitFrame` is what paces the client — the engine's
  own timer stops being the limiter.
- `gl3_vr.c` is compiled into `ref_gles3` too, where the VR path is refused at
  `GL3_VR_GetGLContext`. GLES has no `GL_FRAMEBUFFER_SRGB`, which is why that
  call is guarded.

**Milestone 2 closed** — owner confirmed solid blue in the headset.

---

## Milestone 3 (stereo render path) — built, awaiting headset test

**Works (verified by me)**

- Builds clean, no C4013. Runs with `+map base1` for 30s with a live session
  and **no `[VR]` warnings** — no swapchain or `xrEndFrame` failures.

**First headset test — partial pass, two faults found and fixed**

Owner reported: eyes the right way round, edges clean, **world partially
fuses**, gun doubled, world "very glitchy where some things are
wrong/transparent". Eye order and edge quality confirm the asymmetric
projection and the swapchain geometry are right. Both faults were elsewhere:

1. **No depth buffer on the eye FBO.** The swapchain supplies colour only, and
   a framebuffer is complete with just a colour attachment — so the check
   passed and the world rendered with no depth testing. Fixed by attaching a
   `GL_DEPTH24_STENCIL8` renderbuffer, plus a colour/depth/stencil clear in
   `VR_BeginEye` since the VR frame runs outside `R_BeginFrame`/`R_EndFrame`
   and nothing else was clearing the target.
2. **The gun had its own projection.** `gl3_mesh.c` builds a symmetric frustum
   from `r_gunfov` for `RF_WEAPONMODEL` and overwrites the projection-view
   matrix just before drawing — leaving the weapon as the only object on
   screen projected differently from its surroundings. Fixed to reuse the
   per-eye asymmetric projection. `gl_lefthand`'s X-column mirror is skipped in
   VR, as it would mirror the off-centre terms and break stereo; handedness
   belongs in the weapon pose at milestone 8.

Both are the same lesson from ForsakenVR, hit twice more: **the engine re-sets
your projection further down the pipeline.** Two instances found in gl3 so far
(viewport in `SetupGL`, weapon projection in `gl3_mesh.c`). Assume more.

**Milestone 3 closed** — owner confirmed: world fuses perfectly, artifacts
gone, world size correct.

**`vr_worldscale` default corrected to Team Beef's value.** It was 32 (my
reasoned guess); Quake2Quest ships **26.2467**, arrived at with full locomotion
and playtesting. q3vr's 32 is Quake 3's unit scale and does not transfer.

**Gotcha: an archived cvar masks a changed default.** `config.cfg` already
holds `vr_worldscale "32"` from the first run, so the new default does not
apply to this machine. Set it explicitly to compare. Same will apply to every
`CVAR_ARCHIVE` default we revise from here on.

---

## Milestone 4 (head tracking) — built, awaiting headset test

**Works (verified by me)**

- Builds clean, runs `base1` for 22s with a live session and no warnings.

**What was built**

`VR_ViewAngles` composes the render angles per eye:

- **Yaw** = the game's body yaw **+** head yaw. The body is still steered by
  mouse/stick; the head adds on top. Neither drives the other, which is the
  whole point of the milestone.
- **Pitch and roll** come from the head alone. In VR the neck is the only
  sensible source, and letting mouse pitch through as well would fight it.

The stereo offset is now measured along those same angles, so the eyes separate
across the *head* rather than across the body — otherwise turning your head
would shear the stereo.

Movement direction is untouched: it still comes from the game's own
`cl.predicted_angles`, so looking around cannot steer. Only the render refdef
is modified.

**Known and expected**

- **Yaw is relative to STAGE space**, whose forward direction is wherever the
  guardian was configured. A constant offset between "facing forward in the
  room" and "facing forward in the game" is expected. Recentring is milestone 8.
- Pitch and roll **sign conventions are untested**. `VR_QuatToAngles` maps
  OpenXR's Y-up/-Z-forward onto Quake's Z-up/X-forward, and roll in particular
  is a coin-flip on paper. Per the ForsakenVR rule, this gets settled by looking
  rather than by arguing.

**First headset test — head tracking works, roll was inverted**

Owner confirmed: looking around is decoupled from steering, **pitch correct**,
**roll reversed**. Fixed by negating roll in `VR_QuatToAngles` — it was
extracted about OpenXR's +Z, which points backwards, while Quake rolls about
its forward axis. Exactly the coin-flip the ForsakenVR notes warned about, and
settled by looking rather than by argument, as they advise.

**Awaiting re-test** of roll.

---

## Standing decision: Team Beef's values win (owner, 2026-08-24)

> "The whole project should just be a port of their standalone version to a
> PCVR version. All the work should be done, so just use the Team Beef defaults
> for everything. They have perfected these games."

Full extracted table is in **`TEAMBEEF-DEFAULTS.md`** — 18 cvars, and it is the
spec for milestones 5 through 8. Consult it before inventing any default.

Prompted by `vr_worldscale`: 32 was reasoned from Quake II's 56-unit player
height, felt correct in the headset, and was still wrong against their 26.2467.

Values that would have been expensive to find independently, now already known:
`vr_weapon_pitchadjust -20.0` (a controller is held at an angle to a gun
barrel), `vr_lasersight 2` (on by default, and mode 2 rather than 1),
`vr_snapturn_angle 45`, `vr_weaponscale 0.56`.

**Milestone 4 closed** — owner confirmed roll now correct.

---

## Milestone 5 (controller input) — built, awaiting headset test

**Works (verified by me)**

- Builds clean, no warnings. Action set attaches:
  `[VR] controller input ready (oculus/touch_controller).`

**Not yet verified — needs the owner's hands**

- Whether the sticks actually move and turn, and which way round they are.

**What was built**

New `src/vr/vr_input.c`. The **whole** action set is declared up front — poses,
thumbsticks, triggers, squeeze, face buttons, menu, haptics — even though this
milestone only reads the sticks. OpenXR action sets are immutable once attached
to a session, so adding actions at milestone 6 would mean tearing the session
down and rebuilding it.

Bindings follow Quake2Quest's layout on `/interaction_profiles/oculus/touch_controller`.

- **Locomotion**: left stick → `forwardmove`/`sidemove`, injected in
  `CL_BaseMove` *before* the run multiplier so sprint scales stick movement the
  same way it scales the keyboard's. Feeding the same `usercmd` the keyboard
  uses means the collision body moves normally and nothing downstream needs to
  know VR exists.
- **Stick vector is rotated by head yaw**, so "forward" means where the player
  is looking. Quake2Quest gets this free because their HMD yaw is part of the
  view angles; ours are separate since milestone 4, so it has to be explicit.
- **Turning**: right stick, Team Beef's logic exactly — snap when
  `vr_snapturn_angle > 10` (their default 45, so snap out of the box), latched
  so one push is one snap; continuous below that, gated by `vr_turn_deadzone`.
  Applied to `cl.viewangles[YAW]`, the same path mouse yaw uses, so the body
  turns and movement direction follows.
- **Roomscale**: physical position in the play space displaces the view.
  Horizontal only — the vertical component is real height above the floor and
  the engine already applies its own view height, so using both would stack
  them. Height calibration is milestone 8, where Team Beef keep it.

Cvars adopted from Team Beef: `vr_snapturn_angle 45`, `vr_smoothturn 0`,
`vr_turn_deadzone 0.2`, `vr_walkdirection 1`, `vr_control_scheme 0`.
Ours: `vr_roomscale 1` (they are always roomscale; a seated PC player with a
small tracking volume may not want to be).

**Deviation from Team Beef, deliberate and documented**

Their continuous-turn rate is a fixed amount per *frame*, which on a Quest is a
fixed 72 Hz and therefore a fixed rate. PCVR refresh varies, so the identical
code would turn 25% faster at 90 Hz. Ours scales by frametime against their
72 Hz baseline, preserving the feel they tuned rather than the literal
arithmetic. Only affects continuous turning, which is off by default.

**Known limitation**

Roomscale displaces the view but not the collision body, so a large enough play
space lets the player lean through a wall. Team Beef avoid this by feeding
physical movement into the movement command instead — the better design, and
the follow-up here. They also cancel the run multiplier when they do it, so
head movement covers true walking distance.

**First headset test — both faults were behavioural, not broken plumbing**

Owner reported: walking forward while turning the head left drifted him to the
right; snap turning worked but is the wrong default.

1. **Gaze-directed movement removed.** It was also inverted — turning the head
   left should have pulled him *left* if it were working, so the rotation sign
   was wrong. But it is gone rather than fixed: the owner does not want
   gaze-directed movement at all. Movement is now body-relative, so forward is
   whatever direction the body faces, changed only by stick turning.
   `vr_walkdirection` is now registered but unread.
2. **Smooth turning is the default.** Team Beef default to snap, and select
   modes by whether `vr_snapturn_angle > 10`. Here `vr_smoothturn` is the mode
   switch — which is what its name implies — and defaults to 1.

   Their continuous branch cannot simply be reused: it treats
   `vr_snapturn_angle` as an inverse rate divisor, so at their tuned 45 it
   yields about 16 degrees per second, a crawl. Smooth turning gets
   `vr_turn_speed` (ours, 90 deg/sec), which is also frame-rate independent —
   theirs was constant only because a Quest runs at a locked 72 Hz.

Both are recorded as owner overrides in `TEAMBEEF-DEFAULTS.md`. The standing
policy exists to stop *guessing* at values, not to overrule the owner's
judgement after he has tested something.

**Re-test: movement correct, strafe correct, but still snap turning.**

Cause was **not** the code — it was the archived-cvar trap, already written
down and then walked straight into. `vr_smoothturn "0"` had been archived by
the earlier build, when 0 was its default, and **an archived value beats a
changed default**. Flipping the default to 1 could never have taken effect.

Corrected the stale values directly in `config.cfg` (backed up alongside as
`config.cfg.bak-preVRdefaults`): `vr_smoothturn` 0→1, `vr_walkdirection` 1→0,
`vr_worldscale` 32→26.2467.

> **Process rule from here on: whenever a `CVAR_ARCHIVE` default changes,
> either state the console command in the handoff or fix `config.cfg` — the
> change is invisible on this machine otherwise.** This has cost two test
> rounds now.

**Gaze movement restored as an option, with the sign fixed.**
`vr_walkdirection` is live again: 0 body-relative (default here), 1
gaze-directed (Quake2Quest's behaviour). The earlier inversion is corrected —
Quake's right vector sits 90° clockwise of forward, so the side terms carry the
opposite sign to the naive rotation, which is what drifted the player right
when he turned his head left.

Owner's stated intent, for the record: keep the port as close to stock Team
Beef as possible, with only a couple of deliberate default changes to taste.
Both overrides so far are **defaults only** — snap turning and gaze movement
remain fully available.

**Milestone 5 closed** — owner confirmed movement, strafe and smooth turning.

---

## Milestone 6 (weapon in hand) — half built, awaiting headset test

Split deliberately into two halves so each is separately diagnosable:

| half | state |
|---|---|
| **A. Gun follows the controller** | built, awaiting test |
| **B. Shots leave the barrel** | designed, not yet built |

**Half A — what was built**

`VR_GetWeaponPose` returns the weapon's Quake world pose from the right
controller's aim pose:

- Position taken **relative to the head centre**, then added to the engine's
  camera — the controller and head share a play-space origin, so the difference
  is the hand's position relative to the eyes.
- Basis is the **body yaw**, not the composed head angles. Turning your head
  must not swing your arm around with it.
- The same roomscale displacement the view uses is applied, or the gun drifts
  away from the body as the player walks around the room. `VR_RoomscaleOffset`
  was un-statics'd and shared for exactly this reason.
- `vr_weapon_pitchadjust` (-20°) tilts the model relative to the controller,
  because a controller is held at an angle to a gun barrel.

`CL_AddViewWeapon` uses it when the controller is tracked and falls back to the
engine's stock placement when it is not — so setting a controller down leaves
the gun with the view rather than dropping it at the world origin. The engine's
bob, sway and view-relative offsets are discarded on purpose: they simulate a
hand the player does not have, and in VR the player has one.

Cvars adopted: `vr_weaponscale 0.56`, `vr_weapon_pitchadjust -20.0`,
`vr_weapon_stabilised 0.0`, `vr_lasersight 2`.

**Half B — the design, from Quake2Quest**

Their approach, and it is neat: rather than changing any weapon-firing code,
they temporarily swap the player's `s.origin` and `client->v_angle` to the
*weapon's* pose, call `Think_Weapon`, then restore. All the stock
`P_ProjectSource` maths then fires from the barrel unmodified.

The pose reaches the game DLL through a callback added to `game_import_t`
(`gi.getVROrigins`), which the game pulls each client frame. For us that means
extending `game_import_t`, bumping `GAME_API_VERSION` 3→4, implementing the
callback server-side, and wrapping the two `Think_Weapon` call sites.

**Half A closed** — owner confirmed after two fixes:

- Arm read as too long: position was taken from the **aim** pose, which sits
  forward of the hand because it is a pointing-ray origin. Position now comes
  from the **grip** pose, orientation still from aim.
- Wrist roll inverted: view angles and entity angles are different conventions
  in this renderer. Corrected at the point of use, not in `VR_QuatToAngles`,
  which is verified correct against head tracking — negating it there would
  have broken the head to fix the hand.

**Half B — built, awaiting headset test**

`game_import_t` gains `GetVRWeapon`; `GAME_API_VERSION` 3→4. The game pulls the
weapon pose rather than the engine pushing it, and always tests the pointer —
it is NULL on a dedicated server or a non-VR build, where the game fires from
the view exactly as it always has. `q2ded` builds clean, which confirms no VR
symbols leaked into it.

The swap itself, following Team Beef: rather than teaching every weapon where
the hand is, the *player* is briefly moved to the weapon. Origin and view angle
are exchanged for the muzzle's, `Think_Weapon` runs, originals go back. All the
stock `P_ProjectSource` maths then fires from the barrel with no weapon
changing at all.

Two details that would otherwise put shots in the wrong place:

- **Handedness forced to CENTER for the duration.** `P_ProjectSource` shifts
  the muzzle sideways to sit it beside the player's head, which is exactly
  wrong once the muzzle is already at the hand — it would push every shot about
  a foot right. `CENTER_HANDED` zeroes that term. This is a refinement over
  Team Beef, who leave the lateral offset in.
- **Vertical cancellation.** The offset is measured from the eye, which sits
  `viewheight` above the entity origin, and `P_ProjectSource` then adds
  `viewheight - 8` of its own. The two cancel to a constant +8.

`VR_GetWeaponOffset` is the single source for both the viewmodel and the
firing origin, so the gun the player sees and the muzzle the game shoots from
cannot drift apart. It returns view-convention angles; the viewmodel wrapper
converts to entity convention.

**First test of half B — two faults**

Owner reported: controller buttons do nothing, and firing by mouse put the
bullet a few inches left of the barrel.

1. **Buttons were never consumed.** The action set has carried triggers,
   grips and face buttons since milestone 5, but nothing read them — the
   controllers were tracked and silent. Now wired: right trigger → attack
   (threshold on the analogue value, not the click binding, which fires late),
   left grip or X → use, A → jump, B → crouch.

2. **The muzzle was at the fist, not the barrel — and `aimfix` then moved it
   again.** Two causes stacked:
   - Firing used the **grip** pose. Grip is the hand; the barrel is forward of
     it. `VR_HandPose` now serves both: grip for the model
     (`VR_GetWeaponOffset`), aim for the muzzle (`VR_GetMuzzleOffset`).
   - `aimfix` traces forward from the player's **view** and bends the shot
     toward whatever it hits, so a gun held beside the head still lands on the
     crosshair. In VR there is no crosshair to converge on and the barrel
     already points where the player aimed, so it only bent shots away from
     the barrel — *after* the muzzle was placed, which makes it look like a
     positioning bug. Suppressed for the duration of the swap.

   The earlier claim that `CENTER_HANDED` was the fix for lateral offset was
   premature: it is necessary but was never sufficient. It stays, and is now
   correct for a different reason — the aim pose *is* the barrel line, so any
   sideways term is wrong.

`aimfix` is the third instance of the pattern in `RUNTIME_NOTES.md`: a VR value
set correctly and then quietly replaced further down the pipeline.

**Second test — buttons work, aim still off to the left**

Owner's question was the right one: *"Isn't all this already fixed and set up
with Team Beef's port?"* Yes, and I had been deriving a value they made
adjustable.

**Ported `vr_weapon_adjustment_<weapmodel>`** — one cvar per weapon model,
default `10.0,7.0,-8.0,-3.0,0.0,0.0`: forward, right, up, then pitch, yaw,
roll. Offset is weapon-local, scaled by `vr_weaponscale`, rotated into the
world by the weapon's own orientation, mirrored sideways for a left-handed
`hand` cvar. Also ported their weapon-kick recoil on the model's orientation.

**The lesson, written into `TEAMBEEF-DEFAULTS.md`:** every weapon model is
authored with a different origin, so *no formula places them all correctly*.
Team Beef did not compute an offset — they made one dial per weapon and tuned
each. Two test rounds went into deriving a number that was never derivable.

The adjustment moves the **model only**; shots still leave the raw aim pose, so
tuning a weapon's appearance cannot silently move its point of impact.

**Awaiting re-test — and this one is tunable live rather than needing a build.**

---

## Milestone 7 (HUD and menus) — 7a built, awaiting test

**How Team Beef actually do it.** Not a world-space quad. The HUD stays a 2D
overlay and is **shifted horizontally per eye** — by the eye's optical centre
(needed because our frustums are asymmetric) plus a disparity term from
`vr_hud_ipd / vr_hud_depth`. That is what gives it apparent depth.

That only works if the eye pass draws 2D at all, which ours did not. Hence:

### 7a — the render loop restructure (built)

The eye loop moved **into `SCR_UpdateScreen`**, filling the slot the legacy
`gl1_stereo` path already occupied. Each eye now draws world *and* all 2D.
`V_RenderView(stereo_separation)` is literally the per-eye offset hook, called
inside the loop that draws the status bar, console and menus.

`VR_RenderFrame` is gone, split into `VR_FrameBegin` / `VR_EyeCount` /
`VR_EyeBegin` / `VR_EyeEnd` / `VR_FrameEnd`, plus `VR_ApplyEyeView` which
overrides `cl.refdef` from inside `V_RenderView` — it has to be there, because
`CL_CalcViewValues` rebuilds the refdef every pass and anything applied earlier
is overwritten.

Checked before committing to the approach: `GL3_BeginFrame` only touches
cvar-derived state (gamma, intensity, overbright). No framebuffer binding, no
clears — so binding the eye target around the existing loop is safe.

Throttling is handled: `VR_EyeCount()` returns 0 when the runtime has us
paused, the loop falls back to one flat pass, and `VR_FrameEnd` still submits
an empty frame. An `xrBeginFrame` without a matching `xrEndFrame`
desynchronises the runtime.

**Second instance of the surface-vs-logical trap.** `GL3_SetGL2D` sets the
viewport to `vid.width/height` — with a 640px window and a 3072px eye the HUD
would have landed in a corner. Overridden to the eye size; the ortho still uses
the window's logical space, so the HUD scales up rather than shrinking.

**Dead code removed:** `VR_ParseTestColor` and `VR_WorldReady` existed only
because the eyes used to decide for themselves whether there was a world. The
engine makes that call now, and draws the loading plaque or menu into the eye
instead of a flat colour.

**Verified by me:** both paths build and run clean. Flatscreen with
`vr_enabled 0` still loads `ref_gl3` with no warnings; the VR path reaches
`session running` with a map loaded.

**This re-plumbs working code.** Milestones 3–6 all moved onto the new path, so
the next test is a regression check as much as a feature check.

### 7b — per-eye HUD depth shift (not built)

`vr_hud_depth 0.5`, `vr_hud_ipd 0.064`, `vr_screen_depth 3.5` are registered
but unused. Expect the HUD to be flat — at infinity or fighting the world —
until this lands.

### 7b — per-eye HUD shift (built, confirmed)

Owner confirmed: **milestones 3–6 all survived the restructure**, the HUD and
menus now fuse, and the mirror is readable again.

- **Strobing mirror** was a regression from 7a: with VR on, nothing draws to
  the window's framebuffer, so `R_EndFrame` swapped undefined contents. The
  left eye is now blitted to the window — cheaper than a third scene render.
- **Doubled HUD** was the asymmetric frustum: an eye's optical axis is not the
  centre of its buffer, so centred 2D lands at a different angle per eye and
  diverges. Ported Team Beef's correction — optical centre plus an opposed
  disparity from `vr_hud_ipd / vr_hud_depth` — applied to the ortho bounds so
  every element moves together.

### 7c — status bar placement (built, awaiting test)

Confirmed still missing after 7b, which ruled out "hidden by the doubling" and
left the aspect theory standing.

The eye buffer is **3072×3264** — nearly square — while the HUD's logical space
is 4:3. Mapping one onto the other stretches the HUD vertically and pushes
bottom-anchored elements past what the lens can show; the centred crosshair was
the only survivor. The 2D ortho now matches the eye's aspect, and
**`vr_hud_scale`** (ours, default 1.0) pulls the HUD further towards the middle
— because even a correctly proportioned overlay spanning the whole buffer
reaches into the periphery.

It is a cvar rather than a constant for the same reason Team Beef made the
weapon offsets cvars: how far in it needs to come is a comfort judgement, not a
derivable number.

Verified by me: both VR and flatscreen build and run clean. At
`vr_hud_scale 1.0` the flat path's ortho reduces exactly to the stock bounds.

**Milestone 7 closed** — owner confirmed the HUD is present, fused and
comfortable, and the mirror is readable.

The status bar was **never a placement problem**. Three theories chased
geometry; instrumenting `SCR_DrawStats` answered it in one build:
`state=4 prepped=1 scale=1.00 len=276`, layout string intact. It was being
drawn all along.

The cause was 2D batching. gl3 flushes batched 2D only on a texture change or
at the start of the next `GL3_RenderFrame` — which, under VR, falls inside the
*other eye's* world render. Anything still pending when an eye target was
released went to the wrong eye, or on the last eye to the window. Elements that
change texture often enough (menu, console) flushed themselves and survived;
whatever was drawn last vanished. Now flushed in `GL3_VR_EndEye` and again
before the mirror blit.

**Lesson worth keeping:** reach for instrumentation after the *first* failed
theory, not the fourth.

---

## Milestone 8 (comfort and config) — built, awaiting test

**Handedness.** Uses the stock `hand` cvar rather than adding one — it already
exists, is already archived, and is what Team Beef read for the weapon mirror.
Weapon hand aims, turns and fires; off hand walks and uses. Both swap together.

**Height calibration.** The vertical roomscale term deliberately deferred at
milestone 5 now lands. The engine raises the camera by `viewoffset[2]`, a fixed
standing height; roomscale knows the player's real height above the stage
floor, so it *replaces* that rather than stacking on it — adding both is what
would have made everyone two heads tall. Ducking and leaning come free, since
the measurement drops with the player. `vr_height_adjust` trims it for a
guardian set up on a rug or a raised floor.

Note `player_state_t` has no `viewheight`; the client-facing field is
`viewoffset[2]`. The `viewheight` in `pmove_t` is server-side and unreachable
from here.

**Comfort aperture.** `vr_comfort_mask` (0–1, off by default as theirs is)
closes black bars in from the edges of the eye during **stick** movement only —
walking around the room is real motion the inner ear agrees with and needs no
help. Eased in and out over ~⅓ second, frame-rate independent, because a
vignette that snaps on is itself a motion cue.

Implemented with scissor+clear rather than a blended quad: gl3 is core profile,
so an overlay would need its own shader and vertex buffer. The trade is a hard
edge — an aperture rather than a soft gradient — which is a recognised comfort
mode in its own right and can be softened later if it reads as harsh.

**Snap-turn increments** were already done at milestone 5
(`vr_snapturn_angle`, `vr_smoothturn`, `vr_turn_speed`, `vr_turn_deadzone`).

All settings are `CVAR_ARCHIVE` and persist to `config.cfg`. There is still no
in-game VR options *menu* — the brief asked for cvars persisted to config,
which is done; a menu page would be a natural follow-up now that menus are
readable in the headset.

**First test of milestone 8 — three faults**

1. **Height far too tall** (owner felt ~7ft, taller than the enemies), and a
   seated option wanted.

   Genuine unit bug. The offset subtracted the engine's `viewoffset[2]`, which
   is in **Quake units**, from a value already scaled from **metres**. Correct
   form compares against `QUAKE_MARINE_HEIGHT` (1.57 m, Team Beef's constant)
   *before* scaling:

   ```
   out[2] += (head_m - QUAKE_MARINE_HEIGHT + adjust) * worldscale
   ```

   Arithmetic matches the report exactly: seated at ~1.2 m the old form gave
   +9.5 units where it should give −9.7, about 0.7 m too tall.

   Added **`vr_seated`** (ours — Quake2Quest assumes standing). When set, the
   measured height is ignored and the camera stays at the marine's own eye
   level; leaning still works, only the standing offset is dropped. Faithful
   roomscale would otherwise leave a seated player looking up at everything
   from chair height.

2. **Damage flash was a small square** in the middle of vision. It was drawn at
   the refdef rectangle, but the 2D ortho maps the HUD's logical space onto a
   centred *sub-region* of the eye (`vr_hud_scale`), so the refdef rect covers
   only the middle. `GL3_SetGL2D` now records its ortho bounds and the flash
   fills those instead.

3. **Crosshair followed the head.** A 2D crosshair sits at the centre of vision
   by definition, so it tracks the head and not the gun — worse than none,
   because it looks like an aiming reference without being one. It is now
   suppressed in VR, and `V_Render3dCrosshair` traces from the **muzzle** along
   the barrel instead of from the view along `cl.v_forward`. That reuses the
   engine's existing 3D crosshair as the VR equivalent of Team Beef's laser
   sight, gated on `vr_lasersight` (on by default, as theirs is).

**Awaiting re-test.**

### Next session

1. Owner tests height (with `vr_seated 1`), the flash, and gun-aligned aiming.
2. Then: remaining polish, and optionally a VR options menu page.

**What was built**

- `VR_RenderEye` now runs a real `re.RenderFrame` per eye instead of a flat
  clear. Falls back to the clear colour when there is no world — same two
  guards `V_RenderView` uses (`cls.state == ca_active && cl.refresh_prepped`),
  so menus and loading screens do not render stale refdef state.
- Projection comes from `xrLocateViews` via `VR_ProjectionFromFov` and is
  **asymmetric**. `refexport_t` gained `VR_SetProjection`; `API_VERSION` 9→10.
- `SetupGL` in gl3 now has two VR overrides, both necessary:
  - **Viewport.** The stock maths maps the refdef onto the desktop window. With
    a 480px window and a 3264px eye it computes `y2 = -2784`. VR eyes use
    `(0, 0, eye_w, eye_h)` directly. This is the "surface vs logical size"
    trap ForsakenVR documented, hit exactly as predicted.
  - **Underwater post-processing FBO.** It binds its own framebuffer, which
    would have redirected the eye's geometry away from the swapchain image the
    moment the player entered water — the world would vanish in the headset,
    underwater only. Skipped for VR eyes.
- Culling uses `VR_EnclosingFov`: the smallest *symmetric* fov containing the
  real asymmetric one. The engine's frustum culling assumes symmetry; being
  generous draws slightly too much, being tight would cull geometry visible at
  the lens edge.
- Eye offset is taken relative to the **midpoint of the two eye poses**, so it
  is pure stereo separation with no body movement mixed in — that is milestone
  4, and combining them would make both harder to judge.
- New cvar `vr_worldscale` (default 32, archived): Quake units per metre. Sets
  both apparent world size and stereo separation. Expect to tune by eye.

**Known and expected for this milestone**

- **The world is head-locked.** View angles still come from the game camera, so
  turning your head does not look around — the scene turns with you. That is
  milestone 4. Test briefly rather than lingering.
- `re.RenderFrame` is called outside the usual `R_BeginFrame`/`R_EndFrame`
  pair. The world pass is self-contained so this works, but the 2D/flash tail
  of `GL3_RenderFrame` runs with window-sized coordinates. Cosmetic only until
  milestone 7 puts the HUD in world space.
- Three world renders per frame (one flat, two eyes). The flat one is tiny;
  revisit only if it shows up in profiling.

### Next session

1. Owner reports on stereo: fusion, eye order, edges.
2. Then milestone 4: decouple head orientation from player yaw. `VR_QuatToAngles`
   is already written and waiting.

### Environment notes

- Toolchain is **VS Build Tools 2026** (v18.8, MSVC 14.51, CMake 4.3.1,
  Ninja 1.13.2), plus a 2019 install. **Not** VS 2022 as originally assumed.
- Sibling projects `../ForsakenVR` (PCVR, VDXR+GL, working) and `../NOLFVR` are
  the owner's earlier ports and are the best local reference material —
  `ForsakenVR/VR-NOTES.md` in particular.
