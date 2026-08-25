# Building on Windows with MSVC

Verified 2026-08-23 on Windows 11 with **VS Build Tools 2026** (v18.8,
MSVC 14.51.36231 / cl 19.51) and the bundled CMake 4.3.1. Clean build,
no errors.

This follows upstream's own `.github/workflows/win_msvc.yml` recipe rather
than inventing one. Note that upstream prints *"The CMakeLists.txt is
unmaintained. Use the Makefile if possible."* during configure — their MSVC
CI uses CMake anyway, and so do we, because the VR work needs MSVC. See
"Known risk" below.

---

## 1. Dependencies

SDL2, OpenAL and cURL come prebuilt from `dhewm3-libs`, which is what
upstream CI uses. Fetch **outside** the repo:

```bash
cd E:/Tools/Games/Quake2VR-refs
curl -sL -o dhewm3libs.zip https://codeload.github.com/dhewm/dhewm3-libs/zip/refs/heads/master
unzip -q dhewm3libs.zip "dhewm3-libs-master/x86_64-w64-mingw32/**" -x "dhewm3-libs-master/x86_64-w64-mingw32/share/**"
```

The bundle ships MSVC-format `.lib` files (`SDL2.lib`, `OpenAL32.lib`,
`libcurl.lib`) next to the MinGW `.a` ones, which is why an MSVC build links
against a `-w64-mingw32` directory without complaint.

## 2. Configure

`SDL3_SUPPORT` defaults to **ON**, but the bundle's SDL3 is not what upstream
CI tests on x64 — pass `OFF`.

```bash
cmake -A x64 -DSDL3_SUPPORT=OFF -DYQUAKE2LIBS="E:/Tools/Games/Quake2VR-refs/dhewm3-libs-master/x86_64-w64-mingw32/" -S . -B build
```

CMake auto-selects the newest generator it finds — here `Visual Studio 18 2026`.
Do not pin a generator; that is deliberate, and matches upstream CI.

VR support is on by default (`-DVR_SUPPORT=ON`). The OpenXR loader needs no
installing: CMake fetches and builds it from source, pinned to
`release-1.1.62`, and a post-build step copies `openxr_loader.dll` next to the
executable. Pass `-DVR_SUPPORT=OFF` for a flatscreen-only build; the VR option
is forced off on non-Windows, since the graphics binding is Win32-specific.

## 3. Build

```bash
cmake --build build --config RelWithDebInfo
```

Products land in `build/release/RelWithDebInfo/`:

| | |
|---|---|
| `yquake2.exe` | the client |
| `quake2.exe` | thin wrapper launcher |
| `q2ded.exe` | dedicated server |
| `ref_gl1/gl3/gles3/soft.dll` | renderer modules, loaded at runtime |
| `baseq2/game.dll` | game logic |

`ref_gl3.dll` is our VR target renderer.

## 4. Stage the runtime libraries

```bash
cd build/release/RelWithDebInfo
LIBS=E:/Tools/Games/Quake2VR-refs/dhewm3-libs-master/x86_64-w64-mingw32/bin
cp "$LIBS/SDL2.dll" "$LIBS/OpenAL32.dll" .
cp "$LIBS/libcurl-4.dll" ./curl.dll
```

## 5. Game data — never copied into the repo

Point the engine at an existing retail install instead of copying paks:

```bash
yquake2.exe -datadir "C:\Path\To\Quake2"
```

That directory is the one containing `baseq2\pak0.pak`. `.gitignore` blocks
`*.pak` and the `baseq2/` tree regardless, but `-datadir` means the question
never comes up.

## Smoke test without a headset or a window

`q2ded.exe` exercises the engine, filesystem and game DLL with no GL context
and no window, which makes it the fastest way to tell a broken build from a
broken VR path:

```bat
"%~dp0q2ded.exe" -datadir "E:\Games\Quake 2" +set logfile 2 +map base1
```

A healthy run reaches:

```
------- server initialization ------
50 entities inhibited.
1 teams with 2 entities.
```

With no paks present it stops after `==== Yamagi Quake II Initialized ====`
and waits. Two messages are noise, not faults: `Error getting # of console
events` (stdout was redirected, so there is no console) and
`NET_IPSocket ERROR: getaddrinfo` (no name resolution available).

## The C4013 pointer-truncation trap — read this before debugging a crash

**Include `common.h`, not just `shared.h`, in anything that calls engine
functions.** `Cvar_Get`, `Com_Printf` and friends are declared in `common.h`.

If a translation unit calls `Cvar_Get` without that declaration, C treats it as
implicitly declared and **assumes it returns `int`**. On x64 that silently
truncates the returned 64-bit pointer to 32 bits. The result is a **non-NULL
garbage pointer**: it passes a `if (ptr)` guard and then faults on first
dereference, a long way from the real mistake.

MSVC only emits **warning C4013** for this, so the build looks clean if you are
filtering output for `error`. It cost a crash-to-diagnosis cycle in milestone 2
(`vr_cvars.c` was the victim).

**Grep the build output for `C4013` after adding any new source file.** It is
the single highest-value warning in this codebase:

```bash
cmake --build build --config RelWithDebInfo 2>&1 | grep -E "C4013|C4047|C4024"
```

### How that crash was actually diagnosed

Worth repeating, because the technique generalises and beats guessing:

1. The exit code was `-1073741819` = `0xC0000005`, an access violation.
2. The Windows Application event log named the **faulting module and RVA** —
   `yquake2.exe` at `0xa6f05`. That immediately excluded the OpenXR loader,
   `ref_gl3.dll` and the VDXR runtime.
3. `dumpbin /disasm:nobytes yquake2.exe` resolved that RVA to a function, using
   the default x64 image base of `0x140000000` (so RVA `0xa6f05` is address
   `0x1400a6f05`), and the disassembly showed the exact faulting instruction —
   a NULL check passing, then a dereference dying.

```bash
dumpbin /disasm:nobytes build/release/RelWithDebInfo/yquake2.exe > disasm.txt
```

## CMake gotcha: `--clean-first` with `--target` deletes what it does not rebuild

`cmake --build . --target yquake2 --clean-first` cleans **the whole build tree**
and then builds only `yquake2`, so every renderer DLL is deleted and not
regenerated. The next run fails with `Library ref_gl3.dll cannot be found!` and
falls through every renderer before giving up.

Use `--clean-first` without `--target`, or rebuild all targets afterwards.

## Environment gotcha: cmd will not find an exe in its own directory

This machine has **`NoDefaultCurrentDirectoryInExePath`** set, so `cmd.exe`
does *not* search the working directory for executables. A batch file that
does `cd /d "%~dp0"` and then calls `q2ded.exe` fails with
*"is not recognized as an internal or external command"* even though the file
is sitting right there.

**Always call executables by full path in batch files** — `"%~dp0q2ded.exe"`,
not `q2ded.exe`. This cost time three separate ways (building the OpenXR probe,
running it, and running the dedicated server) before being identified, because
the error message points at a missing file rather than at path resolution.

PowerShell is unaffected: `Start-Process ".\q2ded.exe"` works normally.

---

## Known risk

Upstream regards `CMakeLists.txt` as unmaintained and prefers the Makefile.
The Makefile path is MinGW/GCC, which is not viable for us. We are therefore
on a build path upstream tests in CI but does not consider primary — expect
occasional friction when rebasing on upstream, particularly around new source
files that get added to the Makefile but not to `CMakeLists.txt`.
