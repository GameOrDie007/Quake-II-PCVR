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

**Anything screen-space can be measured with no session at all.** `config.cfg`
holds the eye buffer's size from the last VR run, so a plain launch comes up at
3379x3590 (the log says `setting mode -1: 3379 3590`, and `VR: no OpenXR
instance` when Virtual Desktop is not streaming). `screenshot` then writes the
whole 3379x3590 buffer to `build-mingw/release/baseq2/scrnshot`. HUD placement,
menu layout, UI scale and text size are all decided by those dimensions alone,
so this measures them properly. Drive it with a throwaway
`build-mingw/release/baseq2/probe.cfg` exec'd from the command line:

```
yquake2.exe -portable -datadir "E:\Games\Quake II VR" +exec probe.cfg
```

`map base1`, several hundred `wait` lines, `cmd help` or `cmd inven`, more
waits, `screenshot`, `quit` — one `wait` is one frame, so shoot twice at
different offsets rather than trying to time it exactly. That is how `7049bd3a`
was verified against a rebuild of the previous binary. **Delete `probe.cfg` and
the `.tga` files afterwards.** And *look* at the images: a row-brightness
profile called three of the four shots empty, because the UI is dark green on
dark grey and the panel was plainly there.

## In flight

**Nothing is mid-edit.** The tree is clean. The port is feature-complete and has
been proven on a second machine; what is left is publishing.

### Ready to publish

The archive is built by `python tools/make-dist.py` and is 6.1 MB. Everything
below has been confirmed on a machine that is not the development one:

- Setup finds Quake II, both expansions, the soundtrack and the cutscenes
- Team Beef's HD assets are fetched from their own GitHub release, hash-verified
- The game runs in VR, and the game select page switches between all three
- Menus and the attract demo stay in the world

### Left to do before it goes up

1. **Credit Team Beef for the assets in the README.** Setup now installs their
   artwork with their written permission. Attribution is expected even when
   redistribution is allowed, and this points users at their release page.
2. **Create the repository and push.** There is still no `origin`, only
   `upstream` yquake2. `gh` is authenticated as GameOrDie007. The Quake port is
   at github.com/GameOrDie007/Quake-PCVR and this should match it.
3. **Draft release first**, check the asset and the notes while unlisted, then
   publish in one command.

### The two ports ship together

**His instruction, 6 September 2026: do not push Quake 1 until it and Quake II
are at parity - both fully fixed and ready to release.** Quake 1's full update
is built, verified and committed on `vr-pc`, and is deliberately not pushed. It
carries the same PowerShell setup as this port, in-world menus on by default,
and the `vr_menu_in_world` default fixed. So the remaining work here is what
gates both releases.

### The menu press that needs two goes - now traced in both ports

He reports this in Quake II as well as Quake 1. Both call Team Beef's
`handleTrackedControllerButton` with the same `ovrButton_Enter` to `K_ESCAPE`
mapping, so it is likely one bug, and commit `08adab98` puts the same three-hop
trace here that the Quake port already had: the controller edge in
`VrInputCommon.c`, the top of `Key_Event`, and `M_Menu_Main_f`'s own line.
**Whichever hop is missing from his next log is the one dropping the press.**

The `Key_Event` line prints `key_repeats` and says outright when the autorepeat
guard is about to swallow the press. That guard is the one place here that eats
a key without a trace: a down whose matching up never arrived leaves the count
at 1, and the next down is discarded - exactly the shape of "it needed two
presses".

A healthy press, measured at the desk:

```
Key_Event ESCAPE: down, key_repeats 0, key_dest 0
M_Menu_Main_f: key_dest 0, m_drawfunc null
Key_Event ESCAPE: up, key_repeats 1, key_dest 3
```

The traced build is staged at `E:\Games\Quake II VR\yquake2.exe`. Run with
`developer 1` or the lines will not appear.

**Not fixed, found while reading that path:** `VrInputCommon.c` forward-declares
`Key_Event` with Team Beef's old signature, whose third parameter was `time` and
is now `qboolean special`, and passes `global_time` into it. Every VR button
press therefore arrives flagged special, which skips character insertion in the
console and menu text fields - so a VR controller cannot type into them.
Harmless for escape, which returns before that branch.

### Not started

- **Six weapons need tuning by eye**: Ionripper and Phalanx (The Reckoning);
  Disruptor, ETF Rifle, Plasma Beam, Chainfist (Ground Zero). The Prox Launcher
  does **not** - `v_plaunch` is `v_launch` reskinned, so it takes the Grenade
  Launcher's tuned value. **Team Beef's offsets cannot be derived from geometry**
  - do not try to fit a model to them again.
- **`gl_anisotropic` is declared twice with different values** (`0` and `4`).
  Whichever registers first wins. Setup writes 16 into config.cfg so it does not
  bite today, but it is an accident waiting for someone to remove that line.

### Back burner, by his own call

**Quest 2 only: intro, opening cutscene and first menu are double vision.**
Quest 3 is fine on the same build. Not investigated. All three are
`useScreenLayer()` cases, where the scene is rendered once onto a quad, so
ordinary stereo disagreement should be impossible. `Quest_GetScreenRes` returns
`cylinderSize` rather than the eye buffer size on that path - look there first.

### A real keypress at the desk, without a headset

`SendKeys` does not reach an SDL window - it fails its own control, the
`version` command produces no output either, so a result from it means nothing.
Post the key to the window handle instead and SDL's message loop sees it:

```powershell
Add-Type -Namespace W -Name N -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
'@
# WM_KEYDOWN 0x100 / WM_KEYUP 0x101, VK_ESCAPE 0x1B
[W.N]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]0x1B, [IntPtr]0x00010001)
[W.N]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]0x1B, [IntPtr]0xC0010001)
```

Launch it with `-portable -datadir "E:\Games\Quake II VR" +set vr_enabled 0
+set developer 1 +set vid_fullscreen 0` and redirect stdout to a file. **Quote
the datadir** - unquoted, the space splits it and the engine reports
`-datadir E:\Games\Quake could not be found`.

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
| `4db2ec24` | Menus and the demo stay in the world by default - it had always shipped off |
| `a2b94550` | Take each piece of Quake II from wherever it actually is |
| `1d6798ee` | Do not install Quake 1 as Ground Zero; show why an install was chosen |
| `4dfd7524` | Pick the most complete Quake II; stop asking about the extras |
| `2e803b6e` | Fetch Team Beef's assets from their own release, so we host none |
| `9f91c62e` | Ask Steam where its libraries are, instead of guessing drive letters |
| `d68d968c` | Setup died on a drive letter this machine has and yours does not |
| `dadfa76b` | Setup runs on PowerShell, so a release needs nothing installed |
| `de291964` | Prove the interpreter runs, do not trust "where python" |
| `d696d6e5` | Pre-release audit: our readme on the front page, honest known issues |
| `12b8352d`..`becbece0` | The VR work - menus in world, turning, the demo, the tuner |
| `46e25171` | The release build refuses to ship game data |

**Confirmed in the headset:** pause menu fuses at 3.5m over a lit world; PAUSED
sits correctly; the menu stays put when fixed and follows the gaze when not; snap
and smooth turn are clean and the crosshair and laser sight track them; the
weapon wheel reads at the right size; the demo opens facing the right way and the
stick turns it; the process leak is gone; and all three games load from the
in-game menu.

**Confirmed on a second machine:** Setup with no Python installed, on a network
share, with the install split across two Steam folders and 190 other games
present - finding the game, both expansions, the soundtrack, the cutscenes, and
fetching Team Beef's assets.
