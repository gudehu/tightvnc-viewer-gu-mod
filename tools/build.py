# -*- coding: utf-8 -*-
"""
Build tvnviewer (x64 Release) from the included source.

Usage:
    python tools/build.py

The script locates a Visual Studio 2022 MSBuild installation automatically.
If it cannot find one, set the environment variable VS_MSBUILD to the full
path of MSBuild.exe, e.g.:
    set VS_MSBUILD=C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe

Requires: Visual Studio 2022 with the "Desktop development with C++" workload,
Windows 10 SDK (10.0.22621.0 or compatible), and the v143 toolset.
"""
import os
import subprocess
import sys
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src")
SLN = os.path.join(SRC, "tightvnc2019.sln")
LOG = os.path.join(ROOT, "build.log")
EXE = os.path.join(SRC, "Release", "x64", "tvnviewer.exe")


def find_msbuild():
    env = os.environ.get("VS_MSBUILD")
    if env and os.path.isfile(env):
        return env
    # Common VS2022 installation paths
    pats = [
        r"C:\Program Files\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe",
    ]
    for p in pats:
        hits = glob.glob(p)
        if hits:
            return hits[0]
    return None


def clean_env():
    """MSBuild's StringDictionary is case-insensitive; duplicated proxy vars
    differing only in case cause MSB6001. Keep one spelling per key."""
    env, seen = {}, set()
    for k, v in os.environ.items():
        low = k.lower()
        if low in seen:
            continue
        seen.add(low)
        env[k] = v
    return env


def main():
    msbuild = find_msbuild()
    if not msbuild:
        print("MSBuild not found. Install VS2022 C++ workload or set VS_MSBUILD.")
        return 1
    cmd = [
        msbuild, SLN,
        "/t:tvnviewer",
        "/p:Configuration=Release",
        "/p:Platform=x64",
        "/p:PlatformToolset=v143",
        "/p:WindowsTargetPlatformVersion=10.0.22621.0",
        "/p:VcpkgEnabled=false",
        "/m", "/nologo", "/v:minimal",
        "/fl", "/flp:logfile=" + LOG + ";verbosity=normal",
    ]
    print("BUILD:", " ".join(cmd))
    try:
        r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=900,
                           env=clean_env())
    except subprocess.TimeoutExpired:
        print("BUILD TIMEOUT")
        return 2
    out = (r.stdout or "") + (r.stderr or "")
    for ln in [l for l in out.splitlines() if l.strip()][-60:]:
        print(ln)
    errs = [l for l in out.splitlines()
            if "error " in l.lower() or ": fatal error" in l.lower()]
    print("=" * 60)
    print("exit=%d  error_lines=%d" % (r.returncode, len(errs)))
    if os.path.isfile(EXE):
        print("EXE: %s  %d bytes" % (EXE, os.path.getsize(EXE)))
    return 0 if (r.returncode == 0 and not errs and os.path.isfile(EXE)) else 1


if __name__ == "__main__":
    sys.exit(main())
