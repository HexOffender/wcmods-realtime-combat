"""Pack the mod for a release: dist/realtime-combat-<version>.zip, with the mod's folder at its top (the way wcmods adds a
mod from a .zip): mod.json, settings.json, the built DLL (dlls/main.dll and the dlls/build.json naming the UE4SS it was
built for) and the C++ source, so a player can add it as it is or build it again.

    python tools/pack.py

Build first (`python <wcmods>/wcmods.py cpp build <this repo>/realtime-combat`): it stops when there's no DLL, or when
the DLL is older than a source file (a build since a change was forgotten).
"""
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(HERE, "realtime-combat")
MOD_ID = "realtime-combat"
# What goes in, under the mod's folder (the Visual Studio files and the props stay out: they're for building here).
FILES = ("mod.json", "settings.json", "dlls/main.dll", "dlls/build.json")
SOURCE = "cpp"


def main():
    with open(os.path.join(MOD, "mod.json"), encoding="utf-8") as f:
        version = json.load(f)["version"]
    dll = os.path.join(MOD, "dlls", "main.dll")
    if not os.path.isfile(dll):
        sys.exit("no dlls/main.dll: build the mod first (this file says how)")
    built = os.path.getmtime(dll)
    newer = [os.path.relpath(os.path.join(root, n), MOD)
             for root, _, names in os.walk(os.path.join(MOD, SOURCE)) for n in names
             if os.path.getmtime(os.path.join(root, n)) > built]
    if newer:
        sys.exit(f"dlls/main.dll is older than {', '.join(newer)}: build the mod again first")
    out = os.path.join(HERE, "dist", f"{MOD_ID}-{version}.zip")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in FILES:
            z.write(os.path.join(MOD, name), f"{MOD_ID}/{name}")
        for root, dirs, names in os.walk(os.path.join(MOD, SOURCE)):
            dirs.sort()
            for n in sorted(names):
                path = os.path.join(root, n)
                z.write(path, f"{MOD_ID}/" + os.path.relpath(path, MOD).replace(os.sep, "/"))
    print(out)


if __name__ == "__main__":
    main()
