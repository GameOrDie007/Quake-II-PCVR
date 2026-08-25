# OpenXR runtime notes

Quirks and measured facts about the runtimes this project targets. Append as we
find things; the point is to never rediscover the same bite twice.

Each entry is tagged with its provenance:

- **[measured here]** — observed on this machine, by this project.
- **[carried over]** — from `E:\Tools\Games\ForsakenVR\VR-NOTES.md`, the owner's
  earlier PCVR port on the same machine and the same VDXR runtime (Aug 2026).
  Strong evidence, but re-confirm before relying on it in Quake II.

---

## Hardware this is measured on

GPU is a **GeForce RTX 4070 Ti SUPER**, read from `GL_RENDERER` in
`qconsole.log`. The original project brief said RTX 5080 — that is a different
machine (probably the "Gaming PC" the retail paks were copied from). Everything
here is measured on the 4070 Ti SUPER, which is also the machine Virtual Desktop
streams from, so it is the one that matters.

---

## Runtime inventory (2026-08-23)

**[measured here]**

Only one runtime is registered in
`HKLM\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes`:

```
C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json
```

...and it is also `ActiveRuntime`. SteamVR is installed but **not registered**;
to target it, set `XR_RUNTIME_JSON` explicitly:

```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json
```

| | VirtualDesktopXR | SteamVR/OpenXR |
|---|---|---|
| version | 1.0.10 | 2.16.7 |
| extensions | 31 | 41 |
| `XR_KHR_opengl_enable` | v10 | v12 |
| `XR_KHR_D3D11_enable` | v9 | v11 |
| `XR_KHR_vulkan_enable2` | v2 | v4 |
| `XR_KHR_composition_layer_depth` | v6 | v6 |
| `XR_KHR_visibility_mask` | v2 | v2 |
| `XR_MND_headless` | v2 | v3 |
| `XR_EXT_local_floor` | — | v1 |
| `XR_KHR_locate_spaces` | — | v1 |

Probe source: `../Quake2VR-refs/xrprobe/probe.c`.

---

## Both runtimes reject `apiVersion` 1.1.0

**[measured here]** — `xrCreateInstance` with `XR_MAKE_VERSION(1,1,0)` fails
`XR_ERROR_API_VERSION_UNSUPPORTED` (-4) on **both** runtimes.
`XR_MAKE_VERSION(1,0,34)` succeeds on both.

**Request 1.0.** Do not pass the SDK's `XR_CURRENT_API_VERSION`, which is
1.1.59 in the headers we vendored.

Unresolved: VDXR being 1.0-only is expected, but SteamVR 2.16.7 rejecting 1.1
is not. The probe used an `openxr_loader.dll` borrowed from the NOLFVR project
(July 2026), so **the loader is a plausible culprit rather than the runtime.**

The engine now builds the **official Khronos loader, pinned to
`release-1.1.62`**, via CMake `FetchContent`. That makes the 1.1 question
re-testable: if it is ever worth having a 1.1 feature, change `VR_API_VERSION`
in `src/vr/vr_instance.c` and see whether the borrowed loader was the cause.
Nothing we do needs 1.1, so this is left alone for now.

---

## The Virtual Desktop API layer is always present

**[measured here]** `XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility` is
enumerated as an implicit API layer **even when `XR_RUNTIME_JSON` points at
SteamVR**. It ships from
`C:\Program Files\Virtual Desktop Streamer\openxr-oculus-compatibility.json`.

Implication: "SteamVR" on this machine is not a clean-room SteamVR. If a bug
appears only here and not on other people's SteamVR, suspect this layer before
suspecting the runtime.

---

## `xrGetSystem` needs the headset actively streaming

**[measured here]** With the Quest 3 disconnected (only
`VirtualDesktop.Service` running, no Streamer), `xrGetSystem` returns
`XR_ERROR_FORM_FACTOR_UNAVAILABLE` (-35) on both runtimes.

Extension enumeration and `xrCreateInstance` still work fine, so capability
probing does **not** require the headset — but anything downstream of a system
ID does.

---

## VDXR system properties, headset connected

**[measured here]** Quest 3 streaming over Virtual Desktop:

| | |
|---|---|
| system name | Meta Quest 3 |
| orientation / position tracking | yes / yes |
| max swapchain image | 16384 × 16384 |
| max composition layers | 16 |
| **recommended per-eye** | **3072 × 3264**, 1 sample |
| max per-eye | 16384 × 16384 |
| environment blend modes | `OPAQUE` only |
| **GL version required** | **4.0 – 5.0** |

3072 × 3264 per eye is ~20 MP per frame across both eyes. The earlier Forsaken
measurement of the same figure is now independently confirmed.

Only 16 composition layers, and `OPAQUE` is the only blend mode — no
passthrough. Fine for Quake II, but it rules out any additive-blend HUD trick;
world-space HUD (milestone 7) has to be geometry or a quad layer.

---

## VDXR demands GL 4.0+; Yamagi asks for 3.2 — must be raised

**[measured here]** `xrGetOpenGLGraphicsRequirementsKHR` reports
**min 4.0.0, max 5.0.0**. Yamagi's `gl3` renderer requests a **3.2 core**
context at `src/client/refresh/gl3/gl3_sdl.c:253-255`.

A 3.2 context will be **refused by VDXR**. Milestone 2 must raise the requested
version — Forsaken used a fallback ladder down from 4.6, which is the safe shape
since we must not break flatscreen on machines or drivers that cannot give 4.x.

Note the max is 5.0.0, i.e. an exclusive-feeling upper bound expressed as a
version rather than "no limit". Do not request a context above 4.6.

---

## Probe gotcha: the GL requirements struct type is 1000023005

**[measured here]** `xrGetOpenGLGraphicsRequirementsKHR` returns
`XR_ERROR_VALIDATION_FAILURE` (-1) if `type` is set to `1000023000`. That value
is `XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR`; the requirements struct is
`XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR` = **1000023005**.

Recorded because -1 gives no hint that the *structure type* is what is wrong,
and the two constants differ by five in a block of adjacent OpenGL enums.
Always take these from `openxr.h` rather than typing them.

---

## An OpenXR swapchain gives you colour and nothing else

**[measured here]** The swapchain hands over a colour texture only. A
framebuffer is **"complete" with just a colour attachment**, so attaching the
swapchain image and calling `glCheckFramebufferStatus` reports
`GL_FRAMEBUFFER_COMPLETE` and then renders the entire world **with no depth
testing at all**.

The symptom is not an error. It is geometry drawing through walls and surfaces
looking transparent — which reads as a shader, lighting or renderer fault, a
long way from a missing attachment. Owner described it as "very glitchy where
some things are wrong/transparent".

**Always attach your own depth buffer to a VR eye FBO.** `GL_DEPTH24_STENCIL8`
matches what gl3 uses for its own post-processing FBO, and the stencil is
needed because gl3 draws shadows with it.

Also: because the VR frame runs outside `R_BeginFrame`/`R_EndFrame`, **nothing
else clears the eye target**. Clear colour, depth and stencil in `VR_BeginEye`,
and enable `glDepthMask` first or the depth clear is silently ignored.

---

## The engine will re-set your projection further down the pipeline

**[measured here]** Setting the per-eye projection in `SetupGL` is not enough.
`gl3_mesh.c` builds its **own** symmetric projection for anything flagged
`RF_WEAPONMODEL`, from `r_gunfov`, and overwrites the combined
projection-view matrix just before drawing it.

The result is a world that fuses correctly with a **doubled gun** — the weapon
is the only object on screen projected differently from its surroundings.

This is the same lesson ForsakenVR recorded, in a different place: *if a VR
value looks correct where you set it, check whether the engine replaces it
further down before it is used.* **Assume there are more.**

Instances found so far:

| where | what it replaced | symptom |
|---|---|---|
| `SetupGL`, gl3_main.c | the viewport | eye buffer larger than the window gave a nonsense viewport |
| `gl3_mesh.c` | the projection, for `RF_WEAPONMODEL` | world fused, gun doubled |
| `P_ProjectSource`, weapon.c | the **shot direction**, via `aimfix` | shots left a few inches off the barrel |

The third is worth dwelling on because it is not a rendering value at all.
`aimfix` traces forward from the player's *view* and bends the shot toward
whatever that ray hits, so that a weapon held beside the head still lands on
the crosshair. Sound for flatscreen; actively wrong in VR, where there is no
crosshair to converge on and the barrel already points where the player aimed
it. It runs *after* the muzzle has been positioned, so it presents as a
placement bug rather than as a deliberate correction. Suppressed while the VR
weapon pose is applied.

Related: `gl_lefthand` mirrors the gun by negating the projection's X column.
Applied to an asymmetric frustum that also mirrors the off-centre terms and
breaks stereo, so it is skipped in VR. Handedness belongs in the weapon pose
(milestone 8), not the projection.

---

## A cancelling term hides itself: the QUAKE_MARINE_HEIGHT leak

**[measured here]** The weapon sat "a foot or two" further out than the hand,
and three separate rewrites of the offset geometry changed nothing visible.
That last fact was the clue and it was misread three times.

`VR_RoomscaleOffset` was being added to the weapon offset, and its vertical
term is `(head_height - QUAKE_MARINE_HEIGHT) * worldscale`. Added to an offset
already measured from the head, the head height **cancels algebraically**:

```
(aim.y - head.y)*s  +  (head.y - 1.57)*s  ==  (aim.y - 1.57)*s
```

The gun was positioned relative to a fixed 1.57 m point rather than relative to
the player's head. Confirmed by arithmetic on logged values: the implied head
height was 1.5698 m against a constant of 1.5700 m.

Two lessons, both general:

- **A term that cancels is invisible to experiment.** Changing anything on the
  left-hand side altered nothing, because the result did not depend on it.
  "My change had no effect" should prompt *"is this quantity even in the
  output?"*, not another change.
- **For a standing player the bug is nearly silent**, since `head.y` really is
  about 1.57. It only shows clearly when seated. A bug that hides in the common
  case will be reported as intermittent or as something else entirely.

---

## Multi-monitor desktop: vsync is the thing that bites, not the monitors

**[measured here — setup]** This machine is a two-display workstation: a 75"
TV as the main display and a 14" Corsair Xeneon Edge as the second. Both stay
plugged in.

The monitor layout itself does **not** affect the headset image — VDXR
composites for the Quest independently of the desktop. Two things are worth
knowing anyway:

- **`r_vsync` defaults to 1, and that is actively harmful once a VR session is
  live.** There are then two pacing clocks: the mirror window's vsync, tied to
  whichever monitor the window is on, and `xrWaitFrame`, tied to the headset
  refresh. A 60 Hz desktop will throttle the whole client loop to 60, and the
  headset judders. **This presents exactly like a VR performance problem and is
  not one.** Run VR with `r_vsync 0`; `test-vr.bat` sets it.
- **`vid_displayindex`** (archived, default 0) picks which display the window
  opens on, handled in `src/client/vid/glimp_sdl2.c`. Only matters for where
  the mirror lands. Windowed mode (`vid_fullscreen 0`) is the safe default
  while testing, since a fullscreen grab on the wrong display of a two-monitor
  work setup is disruptive and easy to do by accident.

---

## Carried over from ForsakenVR — confirm before relying on

**[carried over]** These cost real debugging time on the same runtime last
month. Treat as strong priors.

- ~~VDXR requires an OpenGL 4.0–5.0 context~~ — **now measured directly**, see
  above. Forsaken creates 4.6 via a fallback ladder, which is the shape to copy.
- ~~Recommended per-eye resolution 3072×3264~~ — **now measured directly**, see
  above. Forsaken rendered at full recommended resolution fine on this same
  GPU, so full resolution is a reasonable default for us too.
- **Swapchain format must be `GL_SRGB8_ALPHA8`**, with `GL_FRAMEBUFFER_SRGB`
  *disabled* around the blit. A linear `GL_RGBA8` swapchain double-encodes gamma
  and visibly washes the image out. Still unconfirmed for this project — it is
  the first thing to suspect if milestone 2's solid colour looks washed out.
- **The Wi-Fi link dominates framerate and latency, not the engine.** Judder
  that presents exactly like a GPU or CPU limit was the Virtual Desktop stream;
  6 GHz Wi-Fi cleared it completely at full resolution. **Rule the link out
  before profiling anything.**
- **Do not derive coordinate conventions on paper — look through the headset.**
  Three separate bugs (inverted head rotation from a missing transpose, flipped
  eye-offset sign, canted-lens frustum terms in the wrong matrix slots) were
  each settled in one glance and none by argument.
