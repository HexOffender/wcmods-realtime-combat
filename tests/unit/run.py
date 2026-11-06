"""Build and run the unit tests (tests/unit/test_*.cpp) for the mod's rules (realtime-combat/cpp/src/rules.hpp).

    python tests/unit/run.py

Uses g++ or clang++ when one is on PATH (GitHub's CI), else Visual Studio's cl (found with vswhere, its
environment from vcvars64.bat). Each test file is built on its own into build/unit/ and run; the exit code is
non-zero when any fails to build or fails.
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "realtime-combat", "cpp", "src")
OUT = os.path.join(ROOT, "build", "unit")


def vcvars():
    """vcvars64.bat of the newest Visual Studio with the C++ tools, or None."""
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return None
    found = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                           capture_output=True, text=True).stdout.strip()
    bat = os.path.join(found, "VC", "Auxiliary", "Build", "vcvars64.bat") if found else ""
    return bat if os.path.isfile(bat) else None


def build(source, exe):
    """Build one test file. The compiler's output when it fails, else None."""
    for cxx in ("g++", "clang++"):
        if shutil.which(cxx):
            r = subprocess.run([cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", SRC, source, "-o", exe],
                               capture_output=True, text=True)
            return None if r.returncode == 0 else r.stdout + r.stderr
    bat = vcvars()
    if not bat:
        return "no C++ compiler: g++, clang++ or Visual Studio with the C++ tools"
    cmd = (f'call "{bat}" >nul && cl /nologo /std:c++20 /W4 /WX /EHsc /utf-8 /I "{SRC}" "{source}" '
           f'/Fe:"{exe}" /Fo:"{os.path.splitext(exe)[0]}.obj"')
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return None if r.returncode == 0 else r.stdout + r.stderr


def main():
    os.makedirs(OUT, exist_ok=True)
    failed = 0
    for source in sorted(glob.glob(os.path.join(HERE, "test_*.cpp"))):
        name = os.path.splitext(os.path.basename(source))[0]
        exe = os.path.join(OUT, name + (".exe" if os.name == "nt" else ""))
        error = build(source, exe)
        if error:
            print(f"{name}: didn't build\n{error}")
            failed += 1
            continue
        r = subprocess.run([exe], capture_output=True, text=True)
        print(f"{name}: {r.stdout.strip()}")
        if r.returncode != 0:
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
