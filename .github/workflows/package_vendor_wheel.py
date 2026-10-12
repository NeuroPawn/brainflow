#!/usr/bin/env python3
"""Pack and smoke-test one platform's vendored BrainFlow wheel.

The workflow stamps the version only in the runner checkout. setup.py in git
keeps version='0.0.1'.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile


PLACEHOLDER = "version='0.0.1'"
SMOKE_CODE = """
from brainflow.board_shim import BoardShim
from brainflow.data_filter import DataFilter
print(BoardShim.get_package_num_channel(-1))
print(DataFilter.get_version())
"""


def stamp_setup(setup_path, version):
    text = setup_path.read_text()
    stamped = "version='%s'" % version
    if PLACEHOLDER in text:
        setup_path.write_text(text.replace(PLACEHOLDER, stamped, 1))
        return
    if stamped not in text:
        raise SystemExit("setup.py is missing the 0.0.1 version placeholder")


def pack(plat, lib_src, dest, version):
    package_dir = pathlib.Path(os.environ["GITHUB_WORKSPACE"]) / "python_package"
    lib_dst = package_dir / "brainflow" / "lib"
    if lib_dst.exists():
        shutil.rmtree(lib_dst)
    lib_dst.mkdir(parents=True)
    src = pathlib.Path(lib_src)
    files = [path for path in src.iterdir() if path.is_file()]
    if not files:
        raise SystemExit("no libraries in %s" % src)
    for path in files:
        shutil.copy2(path, lib_dst / path.name)
    stamp_setup(package_dir / "setup.py", version)
    for leftover in (package_dir / "dist", package_dir / "build"):
        if leftover.exists():
            shutil.rmtree(leftover)
    for egg in package_dir.glob("*.egg-info"):
        shutil.rmtree(egg)
    subprocess.check_call(
        [sys.executable, "-m", "pip", "install", "setuptools", "wheel"]
    )
    subprocess.check_call([sys.executable, "setup.py", "bdist_wheel"], cwd=package_dir)
    wheels = list((package_dir / "dist").glob("*.whl"))
    if len(wheels) != 1:
        raise SystemExit("expected one wheel, found %s" % wheels)
    subprocess.check_call(
        [
            sys.executable,
            "-m",
            "wheel",
            "tags",
            "--platform-tag",
            plat,
            "--remove",
            str(wheels[0]),
        ]
    )
    wheels = list((package_dir / "dist").glob("*.whl"))
    if len(wheels) != 1:
        raise SystemExit("expected one retagged wheel, found %s" % wheels)
    if plat not in wheels[0].name:
        raise SystemExit("wheel %s is missing platform tag %s" % (wheels[0].name, plat))
    out = pathlib.Path(dest)
    out.mkdir(parents=True, exist_ok=True)
    target = out / wheels[0].name
    shutil.copy2(wheels[0], target)
    print(target)
    return target


def venv_python(venv):
    posix = venv / "bin" / "python"
    if posix.exists():
        return posix
    windows = venv / "Scripts" / "python.exe"
    if windows.exists():
        return windows
    raise SystemExit("venv python not found in %s" % venv)


def smoke(wheel):
    venv = pathlib.Path(os.environ["RUNNER_TEMP"]) / "bf-smoke"
    if venv.exists():
        shutil.rmtree(venv)
    subprocess.check_call([sys.executable, "-m", "venv", str(venv)])
    py = venv_python(venv)
    subprocess.check_call([str(py), "-m", "pip", "install", str(wheel)])
    probe = r"""
import os, pathlib, subprocess, sys
import brainflow
lib = str(pathlib.Path(brainflow.__file__).resolve().parent / "lib")
env = os.environ.copy()
env["LD_LIBRARY_PATH"] = lib + os.pathsep + env.get("LD_LIBRARY_PATH", "")
env["DYLD_LIBRARY_PATH"] = lib + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
code = %r
raise SystemExit(subprocess.call([sys.executable, "-c", code], env=env))
""" % SMOKE_CODE
    subprocess.check_call([str(py), "-c", probe])


def check_entries(wheel, entries):
    names = set(zipfile.ZipFile(wheel).namelist())
    missing = [entry for entry in entries if entry not in names]
    if missing:
        print("wheel is missing:", ", ".join(missing))
        raise SystemExit(1)
    print("wheel contains", ", ".join(entries))


def main(argv):
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    pack_cmd = sub.add_parser("pack")
    pack_cmd.add_argument("--plat", required=True)
    pack_cmd.add_argument("--lib", required=True)
    pack_cmd.add_argument("--dest", required=True)

    smoke_cmd = sub.add_parser("smoke")
    smoke_cmd.add_argument("--wheel", required=True)

    check_cmd = sub.add_parser("check-entries")
    check_cmd.add_argument("--wheel", required=True)
    check_cmd.add_argument("--entry", action="append", required=True)

    args = parser.parse_args(argv)
    if args.cmd == "pack":
        version = os.environ["PYTHON_VERSION"]
        pack(args.plat, args.lib, args.dest, version)
    elif args.cmd == "smoke":
        smoke(pathlib.Path(args.wheel))
    else:
        check_entries(args.wheel, args.entry)


if __name__ == "__main__":
    main(sys.argv[1:])
