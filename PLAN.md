# Quake II PCVR — architecture and plan

Fork of [Yamagi Quake II](https://github.com/yquake2/yquake2), adding 6DoF VR via
OpenXR. Target: Quest 3 over Virtual Desktop (VDXR), SteamVR as fallback.
GPLv2, same as upstream. **No game assets are ever committed.**

Status: **Milestone 0 (survey) complete — awaiting review.** No engine code has
been written.

---

## 1. The graphics-binding question — RESOLVED: use OpenGL

This was flagged as the decision that could cost the whole render path. It is
settled, by measurement on this machine rather than by assumption.

Both runtimes advertise **`XR_KHR_opengl_enable`**:

| Runtime | Version | `XR_KHR_opengl_enable` | Extensions |
|---|---|---|---|
| VirtualDesktopXR | 1.0.10 | **yes, v10** | 31 |
| SteamVR/OpenXR | 2.16.7 | **yes, v12** | 41 |

**Decision: bind OpenGL directly via `XrGraphicsBindingOpenGLWin32KHR`.**

Consequences:

- **No `WGL_NV_DX_interop2`.** The D3D11-interop fallback is not needed. That
  path would have cost a GL↔D3D shared-surface blit every frame plus a class of
  driver bugs we now never have to meet.
- **No SteamVR requirement.** VDXR works directly, which is the primary target.
- Yamagi's `gl3` renderer is usable as-is. No renderer rewrite.

Corroborating evidence: `ForsakenVR/VR-NOTES.md` in the sibling directory
documents a *working* VDXR + OpenGL 4.6 stereo path on this same machine and
runtime, built earlier this month. That is an existence proof, not just an
extension string.

Raw probe output and the probe source live in
`../Quake2VR-refs/xrprobe/` (`probe.c`, run against each runtime via
`XR_RUNTIME_JSON`).

### Measured with the Quest 3 connected

| | |
|---|---|
| system | Meta Quest 3, orientation + position tracking |
| **recommended per-eye** | **3072 × 3264**, 1 sample (~20 MP/frame both eyes) |
| max composition layers | 16 |
| blend modes | `OPAQUE` only — no passthrough |
| **GL version required** | **4.0 – 5.0** |

**Consequence for milestone 2:** Yamagi's `gl3` requests a **3.2 core** context
at `src/client/refresh/gl3/gl3_sdl.c:253-255`. VDXR will refuse it. The
requested version must be raised, via a fallback ladder down from 4.6 so that
flatscreen still works on hardware that cannot give 4.x.

### Caveat carried forward

Both runtimes **rejected `apiVersion` 1.1.0** with `XR_ERROR_API_VERSION_UNSUPPORTED`
and accepted 1.0.34. For VDXR that matches its known 1.0-only nature. For
SteamVR 2.16.7 it is *surprising* — that version should speak 1.1. The probe
used a loader binary borrowed from another project, so **the loader may be the
limiting factor, not the runtime.** We request 1.0 regardless, which works on
both, so this does not block anything. Re-test if we ever want a 1.1-only
feature.

---

## 2. What the three upstreams actually give us

### Yamagi Quake II (the base) — `../Quake2VR-refs/yquake2`, version 8.71pre

Builds on Windows with CMake + MSVC today; ships a `win_msvc.yml` GitHub Actions
workflow we can adapt for the milestone-2 CI artifact.

Renderers present: `gl1`, `gl3`, `gles1`, `gles3`, `soft`. **There is no `gl4`
renderer in current upstream** — the plan targets **`gl3`** (GL 3.2 core, glad
loader).

### Quake2Quest (the VR gameplay logic) — Team Beef

Already OpenXR rather than VrApi, already Yamagi-based. Its VR code is one
directory, `Projects/Android/jni/Quake2VR/`, and it touches the engine in
**22 files**:

```
client/  cl_entities.c cl_input.c cl_inventory.c cl_screen.c
         cl_tempentities.c cl_view.c header/client.h
         menu/menu.c menu/videomenu.c
refresh/ gl1/gl1_main.c gl1/gl1_mesh.c gl1/header/local.h
         gl3/gl3_main.c gl3/gl3_mesh.c
common/  frame.c header/common.h
game/    g_main.c header/local.h player/client.c
         player/weapon.c savegame/savegame.c
```

That is the complete integration surface — the list to work through in
milestones 4–7. The valuable part is `game/player/weapon.c`, where
`P_ProjectSource` has been reworked to take the weapon pose from the controller
instead of the view, plus `vr_weapon_stabilised` and `vr_lasersight`. That is
milestone 6 and it is the fiddliest gameplay work in the project; it is already
solved here.

**Blocking constraint: Quake2Quest vendors yquake2 7.41. Upstream is 8.71pre.**
Roughly four years of engine divergence. We therefore **hand-port its VR hooks
onto modern Yamagi — we do not merge its engine tree.** Merging would silently
revert four years of upstream fixes. This is the single biggest scoping fact
found in the survey and it is why milestones 4–7 are per-file porting work
rather than a diff apply.

### q3vr (the PCVR platform layer) — RippeR37

The structural reference, and it is a good one. Its `code/vr/` is a clean
module split we should mirror almost file-for-file:

```
vr_instance.c   vr_session.c    vr_swapchains.c  vr_spaces.c
vr_renderer.c   vr_render_loop.c vr_updates.c    vr_events.c
vr_input.c      vr_gameplay.c   vr_cvars.c      vr_math.c
vr_virtual_screen.c  vr_haptics.c  vr_debug.c   vr_base.c
```

Confirmed: `vr_session.c` uses `XrGraphicsBindingOpenGLWin32KHR` with
`wglGetCurrentDC()` / `wglGetCurrentContext()` — exactly the path we chose.
It pulls the loader via vcpkg (`openxr-loader`) and a 5-line
`cmake/libraries/openxr.cmake`. We copy that approach.

---

## 3. The structural problem this port has and q3vr did not

**Yamagi builds its renderers as separate DLLs.** `CMakeLists.txt` declares
`ref_gl3` as a CMake `MODULE` library; the client loads `ref_gl3.dll` at runtime
and talks to it through the `refimport_t` / `refexport_t` function-pointer
structs.

So the OpenGL context — which the OpenXR session must be created against, and
which every swapchain blit must run on — lives **on the far side of a DLL
boundary** from where VR state naturally wants to live.

Three ways to resolve it:

**(a) Extend `refexport_t` with a small VR entry-point set. — RECOMMENDED.**
The VR module owns instance, session, spaces, input and poses in the client.
The renderer DLL gains a handful of calls, something like:

```c
qboolean (*VR_BindContext)(void **out_hdc, void **out_hglrc);
void     (*VR_BeginEye)(int eye, unsigned int gl_texture, int w, int h);
void     (*VR_EndEye)(int eye);
```

Keeps the GL work where GL lives, keeps VR logic in one place, and leaves
flatscreen untouched — the calls are simply never invoked with `vr_enabled 0`.
Costs: the interface is versioned, so we bump `API_VERSION` and any third-party
renderer DLL stops loading. Acceptable for a fork.

**(b) Link `ref_gl3` statically into the client for VR builds.** Change one
CMake keyword. Simplest possible fix, but it forks the build shape and we lose
renderer hot-swapping.

**(c) Have the client make GL calls itself** by loading `opengl32.dll` and
resolving the four or five functions the blit needs. `HDC`/`HGLRC` are
process-wide, and Yamagi's renderer runs on the main thread, so this technically
works. Rejected: two independent GL function tables in one process is a
debugging trap.

**DECIDED (owner, 2026-08-23): (a), with (b) held in reserve** if the boundary
proves painful during milestone 2.

---

## 3a. Porting policy: Team Beef's values win

**Decided by the owner, 2026-08-24.** This project is a *port* of Quake2Quest to
PCVR, not a redesign. Where Quake2Quest has a tuned value or a solved
behaviour, we take it as-is.

The full table lives in **`TEAMBEEF-DEFAULTS.md`** and is the spec for
milestones 5 through 8. Consult it before inventing a default.

This was learned the direct way: `vr_worldscale` was set to 32 by reasoning
from Quake II's 56-unit player height. It was defensible, it felt right in the
headset, and it was wrong — Team Beef ship 26.2467, arrived at with full
locomotion and playtesting. Their instrument is better than our reasoning.

Deviate only where the platform genuinely differs — PCVR has no Android
lifecycle, no fixed display rate, a desktop mirror window, and must keep a
working flatscreen mode. When we do deviate, say so in the cvar's comment and
in `PROGRESS.md`.

## 3b. Course correction: port their VR layer, do not reimplement it

**Decided by the owner, 2026-08-24, after repeated weapon-alignment failures.**

`src/vr/` was written as a *from-scratch* VR layer that used Team Beef's
**values**. That is not a port. Every weapon bug this session came from that
gap — my order of operations, my geometry, their constants on top. The clearest
case is documented in `RUNTIME_NOTES.md`: a roomscale term they do not have
cancelled the head position out of the weapon offset algebraically, so three
rewrites of the geometry changed nothing, because the geometry was never in the
answer.

Reading their code and copying one formula at a time reproduces this
indefinitely: **the bugs live in the structure, not the formulas.**

### What is being ported

Their VR layer is engine-agnostic and, critically, **already OpenXR** — their
`ovrQuatf` is literally `XrQuaternionf`. The Android dependencies are confined
to their platform header. Copied verbatim into `src/vr/teambeef/`:

| file | lines | role |
|---|---|---|
| `VrInputDefault.c` | 531 | control schemes, weapon pose, locomotion, turning |
| `VrInputCommon.c` | 150 | button helpers, shared globals |
| `matrixlib.c` | 852 | their matrix maths |
| `mathlib.c` / `.h` | 525 / 215 | their vector maths |
| `VrInput.h`, `VrCvars.h` | 53 | their interfaces |

Their engine call surface is five functions — `Cbuf_AddText`, `Key_Event`,
`Cvar_Get`, `Cvar_Set`, `Cvar_ForceSet` — all of which yquake2 has.

### The only new code

`src/vr/teambeef/VrCommon.h` — a desktop replacement for their Android platform
header, declaring the same types and symbols so their sources compile
unmodified.

`src/vr/vr_teambeef_bridge.c` — **to be written**. It must supply:

- eight globals their platform file owned: `hmdPosition`, `hmdorientation`,
  `weaponoffset`, `weaponangles`, `flashlightoffset`, `flashlightangles`,
  `worldPosition`, `positionDeltaThisFrame`
- six helpers: `radians`, `degrees`, `QuatToYawPitchRoll`, `isMultiplayer`,
  `GetTimeInMilliSeconds`, `useScreenLayer`
- a per-frame fill of their `ovrTracking` / `ovrInputStateTrackedRemote` structs
  from our OpenXR poses and actions, then a call to `HandleInput_Default`

Everything downstream — offsets, angles, weapon adjustment, laser sight, control
schemes, snap turn — becomes theirs verbatim and stops being something this
project can get subtly wrong.

### What stays ours

The genuinely PCVR-specific half, which has been working and has no equivalent
in their build: OpenXR instance/session/swapchains, the OpenGL binding, the
eye-render loop through `SCR_UpdateScreen`, the desktop mirror, and the
flatscreen fallback.

**That split is the reusable part.** The platform half transfers to VRaze and
Prey 2006; only the game half changes, and that comes from each game's Team Beef
port rather than being rebuilt.

## 4. Proposed layout

```
src/vr/                    new — VR module, mirrors q3vr's split
  vr_base.[ch]             lifecycle, cvar registration, enable/disable
  vr_instance.[ch]         XrInstance, extension selection, system query
  vr_session.[ch]          XrSession + GL binding, session state machine
  vr_swapchains.[ch]       colour/depth swapchains, image acquisition
  vr_spaces.[ch]           reference spaces, roomscale origin
  vr_render_loop.[ch]      xrWaitFrame/BeginFrame/EndFrame, layer submission
  vr_input.[ch]            action sets, bindings, controller poses
  vr_math.[ch]             XrPosef <-> Quake vec3_t/mat4, projection from FOV
  vr_cvars.[ch]            all vr_* cvars
CMakeLists.txt             + OpenXR loader, + src/vr, VR entry points on ref_gl3
PLAN.md  PROGRESS.md  RUNTIME_NOTES.md
```

**Runtime check, never a compile-time fork.** A single `vr_enabled` cvar gates
every VR path; the flatscreen build is the same binary with VR off. This is a
working rule from the brief and it also happens to be how the fallback stays
trustworthy — a compile-time fork rots the moment nobody builds it.

OpenXR loader comes in via vcpkg manifest, matching q3vr, so CI and local builds
resolve it identically.

---

## 5. Milestones, with what "done" means

| # | Goal | Verified by |
|---|---|---|
| 0 | Survey, probe, plan | **this document — your review** |
| 1 | Flatscreen Yamagi, MSVC, your paks | plays on the monitor |
| 2 | OpenXR instance/session/swapchains/frame loop, solid colour per eye | colour in headset, framerate holds, clean log |
| 3 | Stereo `R_RenderFrame` ×2, projection from `xrLocateViews` | stereo fuses, no eye-swap, no edge warp |
| 4 | Head tracking decoupled from player yaw | look behind you while walking forward |
| 5 | Controller input, roomscale offset | smooth locomotion, snap/smooth turn |
| 6 | Weapon in hand, fire direction via `P_ProjectSource` | aim independently of gaze |
| 7 | HUD and menus in world space | readable status bar, usable console/menu |
| 8 | Comfort and config cvars, persisted | vignette, turn increment, height, handedness |

Milestone 2 also lands the GitHub Actions Windows artifact build.

---

## 6. Risks, in the order they are likely to bite

1. **The renderer DLL boundary** (§3). Highest-uncertainty item. Front-loaded
   into milestone 2 deliberately.
2. **Engine divergence** (§2). Every Quake2Quest hook must be re-read against
   8.71pre rather than applied. Expect the `gl3` hooks to have moved most.
3. **Coordinate conventions.** ForsakenVR needed three separate sign/transpose
   fixes to make stereo fuse, and its notes are explicit that *every one was
   found by looking through the headset, not by reasoning*. Budget headset test
   rounds for milestone 3 rather than trying to derive the answer.
4. **sRGB.** ForsakenVR found VDXR needs a `GL_SRGB8_ALPHA8` swapchain with
   `GL_FRAMEBUFFER_SRGB` disabled around the blit; a linear `GL_RGBA8`
   swapchain double-encodes gamma and washes out. Recorded in RUNTIME_NOTES.md
   so we do not rediscover it.
5. **Toolchain drift.** You have Build Tools **2026 (v18.8, MSVC 14.51)** and
   2019 — not 2022 as the brief assumed. 2026 is fine and is what the probe was
   built with; noting it so CI matches.
6. **Wi-Fi masquerading as GPU load.** Also from the Forsaken work: judder that
   looks exactly like a frame-rate problem was the Virtual Desktop stream. Rule
   the link out before profiling anything.

---

## 7. Review outcome

Reviewed by the owner on 2026-08-23. Milestone 0 is closed.

- §3 renderer-DLL boundary — **(a) extend `refexport_t`, with (b) linking
  `ref_gl3` statically held in reserve.** Accepted as recommended.
- §2 hand-port rather than merge — accepted.
- Milestone order — unchanged.
- Runtime capabilities measured with the headset connected; results folded into
  §1 and `RUNTIME_NOTES.md`.

Proceeding to milestone 1: build unmodified Yamagi with MSVC and confirm it
plays flatscreen with the retail paks.

### Re-running the probe

The probe lives outside the repo, in `../Quake2VR-refs/xrprobe/`. From
**PowerShell** (note: not `cmd` syntax — no `cd /d`, and Windows PowerShell 5.1
has no `&&`):

```bash
& "E:\Tools\Games\Quake2VR-refs\xrprobe\xrprobe.exe" "VDXR"
```

To probe SteamVR instead, set `$env:XR_RUNTIME_JSON` to
`C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`
first, and remove it afterwards.
