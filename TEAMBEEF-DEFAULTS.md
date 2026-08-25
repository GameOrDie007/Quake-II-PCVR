# Team Beef defaults — the reference values for this port

**Standing project decision (owner, 2026-08-24):**

> The whole project should just be a port of their standalone version to a
> PCVR version. All the work should be done, so just use the Team Beef defaults
> for everything. They have perfected these games.

So: **when Quake2Quest has a value, we take it.** Do not re-derive, do not
round off, do not "improve" it. These numbers came from a shipped, playtested
Quest port; reasoning from first principles is a worse instrument than their
tuning, and `vr_worldscale` already proved that — a defensible guess of 32 was
wrong against their 26.2467.

Deviate only when the *platform* forces it (PCVR has no Android lifecycle, no
fixed refresh, and a desktop mirror window), or when the owner overrides after
testing — he is the final authority on feel, and this policy exists to stop
*guessing*, not to overrule him. Either way, say so explicitly in the cvar's
comment and in `PROGRESS.md`.

## Owner overrides, after headset testing

| behaviour | Team Beef | here | why |
|---|---|---|---|
| Turning | snap by default | **smooth by default** | Owner preference, 2026-08-24 |
| Movement direction | gaze-directed | **body-relative** | Tested and disliked — looking around dragged the player off course |

Both were tested in the headset before being changed, which is the only
evidence that counts for either.

**Both are default changes only.** Snap turning (`vr_smoothturn 0`) and
gaze-directed movement (`vr_walkdirection 1`) remain fully available. The
owner's stated intent is to keep this as close to stock Quake2Quest as
possible, changing only a couple of defaults to taste — so a deviation should
mean "a different default", never "the behaviour is gone".

Source: `Quake2Quest/Projects/Android/jni/`, GPLv2 — same licence as this fork,
so adopting values and design is both legal and intended. Credit belongs to
Team Beef.

---

## The full set, extracted from Quake2Quest

| cvar | default | milestone | notes |
|---|---|---|---|
| `vr_worldscale` | **26.2467** | 3 ✅ | Quake units per metre. **Adopted.** |
| `vr_control_scheme` | 0 | 5 | Controller layout selection |
| `vr_walkdirection` | 1 | 5 | Walk relative to hand vs head |
| `vr_snapturn_angle` | 45 | 5 | Degrees per snap |
| `vr_smoothturn` | 0 | 5 | Snap by default, not smooth |
| `vr_turn_deadzone` | 0.2 | 5 | Stick deadzone before turning |
| `vr_weaponscale` | 0.56 | 6 | Viewmodel scale in hand |
| `vr_weapon_pitchadjust` | -20.0 | 6 | Weapon pitched down relative to the controller |
| `vr_weapon_stabilised` | 0.0 | 6 | Two-handed weapon hold off by default |
| `vr_lasersight` | 2 | 6 | **On by default**, mode 2 — not 0/1 |
| `vr_hud_depth` | 0.5 | 7 | HUD distance |
| `vr_hud_ipd` | 0.064 | 7 | HUD stereo separation, metres |
| `vr_screen_depth` | 3.5 | 7 | Virtual screen distance (menus, cinematics) |
| `vr_comfort_mask` | 0.0 | 8 ✅ | Vignette off by default. **Adopted.** |
| `vr_height_adjust` | 0.0 | 8 ✅ | Manual height calibration. **Adopted.** |
| `vr_jump_sound` | 1 | — | |
| `vr_use_wheels` | 1 | — | |
| `vr_framerate` | 0 | — | Quest-specific display rate; likely N/A on PCVR |

`vr_weapon_pitchadjust = -20.0` is worth calling out as the kind of value that
would cost a long evening to find by trial: it exists because a Quest controller
is held at an angle to the barrel of a gun, and it is not something the code
would suggest on its own.

## Per-weapon calibration — the one that is not a single cvar

`vr_weapon_adjustment_<weapmodel>`, default **`10.0,7.0,-8.0,-3.0,0.0,0.0`**,
one per weapon model index. Six values: forward, right, up, then pitch, yaw,
roll. The offset is in weapon-local axes, scaled by `vr_weaponscale`, rotated
into the world by the weapon's own orientation, and mirrored sideways when the
stock `hand` cvar is left-handed.

**This is the answer to "why doesn't the bullet come out of the barrel".** Every
weapon model is authored with a different origin, so there is no formula that
places them all correctly — Team Beef did not derive an offset, they made one
dial per weapon and tuned each. Any attempt to compute this from pose geometry
is solving the wrong problem, and this project spent two test rounds proving it.

The adjustment moves the **model only**. Shots leave the raw aim pose, so
tuning a weapon's appearance never silently moves its point of impact.

## Ours, with no Quake2Quest equivalent

These exist because PCVR is a different platform, not because we disagreed with
anything:

| cvar | default | why it is ours |
|---|---|---|
| `vr_enabled` | 0 | Quake2Quest is always VR. We must keep a working flatscreen mode, so VR is opt-in. |
| `vr_resolution_scale` | 100 | Desktop GPUs vary enormously; the Quest does not. |
| `vr_test_color` | `0.1 0.35 0.6` | Bring-up diagnostic from milestone 2, kept as a way to prove the compositor still works when the world stops drawing. |
| `vr_debug` | 0 | Frame-loop logging. |

## Gotcha when changing a default

`CVAR_ARCHIVE` values already written to `config.cfg` **override a changed
default** — the new value will not appear on a machine that has run an earlier
build. Set it explicitly in the console, or edit `config.cfg`, when comparing.

On this machine that file is OneDrive-redirected:
`C:\Users\Miles\OneDrive\Documents\YamagiQ2\baseq2\config.cfg`
