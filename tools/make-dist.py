#!/usr/bin/env python3
"""
Build the release archive, and refuse to build one that carries game data.

Game data is never ours to distribute, and neither is anything generated from
it: the weapon wheel icons are cut out of the game's own HUD pics, so a wheel
folder in the archive is as much a problem as a pak would be. All of it is built
on the player's machine by Setup, from the player's own copy.

Remembering that at the end of a long day is not a plan, so this checks. The
rule is narrow enough to state in one line:

    inside a gamedir, only game.dll may ship.

Everything else in baseq2/, xatrix/ or rogue/ is either the game's or derived
from it. At the top level only the binaries, the tools and the text files are
allowed, by name.

Usage: python tools/make-dist.py [output-directory]

  output-directory  defaults to the parent of the repository
"""

import os
import shutil
import subprocess
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GAMEDIRS = ("baseq2", "xatrix", "rogue", "ctf")

# What a gamedir is allowed to contain in the archive.
GAMEDIR_ALLOWED = {"game.dll", "game.so", "game.dylib"}

# What may sit at the top level, by name.
TOP_ALLOWED = {
    "yquake2.exe", "quake2.exe", "q2ded.exe",
    "ref_gl1.dll", "ref_gl3.dll", "ref_soft.dll",
    "SDL2.dll", "libopenxr_loader.dll", "libgcc_s_seh-1.dll",
    "libstdc++-6.dll", "libwinpthread-1.dll", "openal32.dll",
    "Setup.bat", "README.txt", "LICENSE", "LICENSE.txt",
}

TOOLS_ALLOWED_EXT = {".py", ".txt", ".sh"}


def audit(root):
    """Return a list of complaints. Empty means it is safe to ship."""
    bad = []

    for entry in sorted(os.listdir(root)):
        path = os.path.join(root, entry)

        if os.path.isdir(path):
            if entry in GAMEDIRS:
                for name in sorted(os.listdir(path)):
                    if name not in GAMEDIR_ALLOWED:
                        kind = "directory" if os.path.isdir(os.path.join(path, name)) else "file"
                        bad.append(f"{entry}/{name}  ({kind} in a gamedir - "
                                   f"only {'/'.join(sorted(GAMEDIR_ALLOWED))} may ship)")
            elif entry == "tools":
                for name in sorted(os.listdir(path)):
                    if os.path.splitext(name)[1] not in TOOLS_ALLOWED_EXT:
                        bad.append(f"tools/{name}  (not a script or a data list)")
            else:
                bad.append(f"{entry}/  (unexpected directory)")
        elif entry not in TOP_ALLOWED:
            bad.append(f"{entry}  (not on the allowed list)")

    return bad


def main():
    out_root = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 \
        else os.path.dirname(REPO)

    branch = subprocess.run(["git", "-C", REPO, "rev-parse", "--abbrev-ref", "HEAD"],
                            capture_output=True, text=True, check=True).stdout.strip()
    sha = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
                         capture_output=True, text=True, check=True).stdout.strip()

    name = f"Quake2VR-{branch}-{sha}"
    staged = os.path.join(out_root, name)

    if os.path.isdir(staged):
        shutil.rmtree(staged)

    env = dict(os.environ, NODATA="1")
    subprocess.run(["bash", os.path.join(REPO, "tools", "package-release.sh"), staged],
                   env=env, check=True)

    # The engine is GPLv2, so the licence travels with the binary. The complete
    # corresponding source is the public repository.
    for licence in ("LICENSE", "LICENSE.txt", "COPYING"):
        source = os.path.join(REPO, licence)
        if os.path.isfile(source):
            shutil.copyfile(source, os.path.join(staged, "LICENSE"))
            break
    else:
        print("\nREFUSING: no licence file found in the repository.")
        print("The engine is GPLv2 and the licence has to ship with the binary.")
        return 1

    print()
    complaints = audit(staged)

    if complaints:
        print("REFUSING to build an archive - this is not ours to distribute:")
        for line in complaints:
            print("  " + line)
        print()
        print(f"Left staged at {staged} so you can look.")
        return 1

    archive = staged + ".zip"

    if os.path.isfile(archive):
        os.remove(archive)

    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for folder, _dirs, files in os.walk(staged):
            for f in sorted(files):
                full = os.path.join(folder, f)
                z.write(full, os.path.join(name, os.path.relpath(full, staged)))

    # What the tester ran has to be what the archive holds. Print the hash of
    # the engine binary inside the archive so it can be checked against the one
    # that was actually confirmed working.
    import hashlib

    with zipfile.ZipFile(archive) as z:
        digest = hashlib.sha256(z.read(f"{name}/yquake2.exe")).hexdigest()

    print(f"clean: {len(os.listdir(staged))} top-level entries, no game data")
    print(f"archive: {archive}  ({os.path.getsize(archive) / 1e6:.1f} MB)")
    print(f"yquake2.exe in the archive: sha256 {digest[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
