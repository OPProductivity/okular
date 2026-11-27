#!/usr/bin/env python3
# SPDX-FileCopyrightText: None
# SPDX-License-Identifier: CC0-1.0
"""Provision a new Windows Craft prefix from public sources and build this checkout."""

from __future__ import annotations

import argparse
import configparser
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import runpy
import shutil
import sqlite3
import subprocess
import sys
import venv


CRAFT = "cae7aeac791e170c157eed18bb037e1fad8edf57"
BLUEPRINTS = "bdeeed04c1ef40770049b9d0cadba99bface4221"
ABI = "windows-cl-msvc2022-x86_64"
SOURCE = Path(__file__).resolve().parents[1]
GUIDE = "https://github.com/OPProductivity/okular/blob/master/docs/windows-craft-build.md#automated-public-build"
OKULAR = "kde/applications/okular"
FEATURE_PACKAGES = (
    "libs/qt/qtsvg", "libs/qt/qtmultimedia",
    "kde/frameworks/tier2/kdoctools", "kde/frameworks/tier2/kcolorscheme",
)


def run(log: Path, argv, env, *, cwd=None):
    print(f"{log.stem}: {log}", flush=True)
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"\n> {subprocess.list2cmdline(list(map(str, argv)))}\n")
        stream.flush()
        result = subprocess.run(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError(f"{log.stem} failed with exit code {result.returncode}. See {log}.")


def checkout(work: Path, folder: Path, name: str, commit: str, env):
    log = work / "git.log"
    folder.mkdir(parents=True, exist_ok=True)
    if not (folder / ".git").exists():
        run(log, ["git", "init", folder], env)
    head = subprocess.run(["git", "-C", folder, "rev-parse", "HEAD"],
                          env=env, capture_output=True, text=True).stdout.strip()
    if head != commit:
        if subprocess.check_output(["git", "-C", folder, "status", "--porcelain"],
                                   env=env, text=True).strip():
            raise ValueError(f"Craft checkout has local changes: {folder}")
        run(log, ["git", "-C", folder, "fetch", "--depth=1",
                  f"https://github.com/KDE/{name}.git", commit], env)
        run(log, ["git", "-C", folder, "switch", "--detach", "FETCH_HEAD"], env)


def initialize_settings(work: Path, prefix: Path, tree: Path, python: Path):
    settings = configparser.ConfigParser(interpolation=None)
    settings.optionxform = str
    with (tree / "CraftSettings.ini.template").open(encoding="utf-8") as template:
        settings.read_file(template)
    settings["General"]["ABI"] = ABI
    settings["General"]["KFHostToolingVersion"] = "6"
    settings["Paths"]["Python"] = str(python.parent)
    settings["Compile"]["UseNinja"] = "False"
    settings["Compile"]["MakeProgram"] = "jom"
    settings["ShortPath"]["JunctionDir"] = str(work / "short")
    settings["BlueprintVersions"]["EnableDailyUpdates"] = "False"
    settings["Packager"]["RepositoryUrl"] = "https://files.kde.org/craft/Qt6/"
    etc = prefix / "etc"
    etc.mkdir(exist_ok=True)
    # pkgconf's Muon dependency otherwise pulls Ninja in even with UseNinja disabled.
    (etc / "BlueprintSettings.ini").write_text("[dev-utils/ninja]\nignored = True\n", encoding="utf-8")
    with (etc / "CraftSettings.ini").open("x", encoding="utf-8") as output:
        settings.write(output)


def apply_pdf_recipes(work: Path, blueprints: Path, env):
    overrides = SOURCE / "docs/craft-overrides"
    patch = overrides / "pdf-dependencies-2026-11-17.patch"
    apply = ["git", "-C", blueprints, "apply"]
    reversed_check = subprocess.run([*apply, "--reverse", "--check", patch], env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if reversed_check.returncode:
        run(work / "pdf-recipes.log", [*apply, "--check", patch], env)
        run(work / "pdf-recipes.log", [*apply, patch], env)
    name = "libspectre-0.2.12-windows.diff"
    destination = blueprints / "libs/libspectre" / name
    expected = (overrides / name).read_bytes()
    if destination.exists():
        if destination.read_bytes() != expected:
            raise ValueError(f"The pinned libspectre patch differs from {destination}")
    else:
        destination.write_bytes(expected)


def prefer_msys_primary(prefix: Path) -> bool:
    changed = False
    for path in (prefix / "msys/etc/pacman.d").glob("mirrorlist.*"):
        text = path.read_text(encoding="utf-8")
        servers = [line for line in text.splitlines() if line.startswith("Server = ")]
        primary = next((line for line in servers if line.startswith("Server = https://repo.msys2.org/")), None)
        if primary and servers[0] != primary:
            text = text.replace(primary + "\n", "", 1).replace(servers[0], primary + "\n" + servers[0], 1)
            path.write_text(text, encoding="utf-8")
            changed = True
    return changed


def provision(log: Path, command, env, prefix: Path):
    start = log.stat().st_size if log.exists() else 0
    try:
        run(log, command, env)
    except ValueError:
        with log.open("rb") as stream:
            stream.seek(start)
            output = stream.read()
        if (b"pacman" not in output or not any(error in output for error in (b"error: 429", b"Connection timed out"))
                or not prefer_msys_primary(prefix)):
            raise
        print("Retrying with the primary MSYS2 package mirror for this workspace.", flush=True)
        run(log, command, env)


def bootstrap(work: Path, prefix: Path, env):
    python_dir = work / "bootstrap-python"
    python = python_dir / "Scripts/python.exe"
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(python_dir)
    run(work / "pip.log", [python, "-m", "pip", "--disable-pip-version-check", "install", "certifi"], env)

    craft = prefix / "craft/bin/craft.py"
    tree = prefix / "craft-tmp"
    if not (prefix / "etc/CraftSettings.ini").is_file():
        checkout(work, tree, "craft", CRAFT, env)
        initialize_settings(work, prefix, tree, python)
    completed = work / "bootstrap-complete"
    if not completed.is_file():
        entry = craft if craft.is_file() else tree / "bin/craft.py"
        if not entry.is_file():
            raise ValueError(f"Incomplete Craft bootstrap in {prefix}. See {work / 'bootstrap.log'}.")
        provision(work / "bootstrap.log", [python, "-u", entry, "craft"], env, prefix)
    if not craft.is_file():
        raise ValueError(f"Craft did not install its command-line entry point under {prefix}")

    checkout(work, prefix / "craft", "craft", CRAFT, env)
    blueprints = prefix / "etc/blueprints/locations/craft-blueprints-kde"
    checkout(work, blueprints, "craft-blueprints-kde", BLUEPRINTS, env)
    apply_pdf_recipes(work, blueprints, env)
    prefer_msys_primary(prefix)
    completed.touch()
    return [python, "-u", craft]


def preserve_empty_git_config(env: dict):
    count = int(env.get("GIT_CONFIG_COUNT", "0"))
    values = [env.get(f"GIT_CONFIG_VALUE_{index}") for index in range(count)]
    if not values or "" not in values:
        return
    # Craft resets os.environ while bootstrapping. Windows loses empty
    # indexed Git values in that round trip, so preserve every setting and
    # its precedence using Git's command-line parameter representation.
    parameters = []
    for index, value in enumerate(values):
        key = env.get(f"GIT_CONFIG_KEY_{index}")
        if key is None or value is None:
            raise ValueError(f"Incomplete inherited Git configuration at index {index}")
        argument = f"{key}={value}"
        parameters.append("'" + argument.replace("'", "'\\''") + "'")
    for index in range(count):
        del env[f"GIT_CONFIG_KEY_{index}"]
        del env[f"GIT_CONFIG_VALUE_{index}"]
    del env["GIT_CONFIG_COUNT"]
    parameters.append(env.get("GIT_CONFIG_PARAMETERS", ""))
    env["GIT_CONFIG_PARAMETERS"] = " ".join(filter(None, parameters))


def environment(vcvars: Path, work: Path) -> dict:
    helper = runpy.run_path(str(SOURCE / "scripts/windows-craft-recovery.py"))["developer_environment"]
    env = helper(vcvars, work)
    # An installation launched from another Craft or Qt shell must use this prefix.
    forbidden = ("CRAFT", "KDEROOT", "QTDIR", "QT_", "QML", "CMAKE_", "VCPKG_", "PKG_CONFIG",
                 "VIRTUAL_ENV", "PYTHONHOME", "PYTHONPATH", "OKULAR_TEST_INSTANCE")
    for key in list(env):
        if key.startswith(forbidden):
            del env[key]
    paths = []
    for value in env["PATH"].split(os.pathsep):
        if value:
            path = Path(value)
            if not any((parent / "etc/CraftSettings.ini").is_file() for parent in (path, *path.parents)):
                paths.append(value)
    env["PATH"] = os.pathsep.join(paths)
    if env.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x64":
        raise ValueError("Use the Visual Studio 2022 x64 vcvars64.bat.")
    missing = [tool for tool in ("git.exe", "cl.exe", "link.exe", "nmake.exe", "rc.exe", "mt.exe")
               if not shutil.which(tool, path=env["PATH"])]
    if missing:
        raise ValueError(f"Missing host tools: {', '.join(missing)}. See {GUIDE}")
    preserve_empty_git_config(env)
    return env


def check_dependency_paths(cache: Path, prefix: Path):
    entries = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator and ":" in key:
            entries[key.split(":", 1)[0]] = value
    required = (
        "ECM_DIR", "Qt6_DIR", "Qt6Svg_DIR", "Qt6Qml_DIR", "Qt6TextToSpeech_DIR",
        "Qt6MultimediaWidgets_DIR", "KF6DocTools_DIR", "Poppler_Qt6_LIBRARY", "LIBSPECTRE_LIBRARY",
    )
    foreign = [name for name in required if not Path(entries.get(name, "")).is_relative_to(prefix)]
    if foreign:
        raise ValueError(f"CMake did not find these dependencies in {prefix}: {', '.join(foreign)}")


def build(work: Path, vcvars: Path, cmake: Path | None, dependencies_only: bool):
    marker = work / "public-build.json"
    expected = {"format": 1, "abi": ABI, "craft": CRAFT, "blueprints": BLUEPRINTS}
    if marker.is_file():
        if json.loads(marker.read_text(encoding="utf-8")) != expected:
            raise ValueError("This workspace belongs to another Craft profile. Choose a new workspace.")
    else:
        if any(entry.name != ".public-build.lock" for entry in work.iterdir()):
            raise ValueError("Use an empty workspace for the first run.")
        marker.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
    prefix = work / "prefix"
    env = environment(vcvars, work)
    replay = runpy.run_path(str(SOURCE / "scripts/windows-craft-prefix.py"))
    if cmake:
        replay["require_files"](cmake.parent, (cmake.name, "ctest.exe"))
    craft = bootstrap(work, prefix, env)
    cmake = cmake or prefix / "dev-utils/bin/cmake.exe"
    replay["require_files"](cmake.parent, (cmake.name, "ctest.exe"))
    # The MSVC NMake fallback also works when these Qt packages miss the binary cache.
    provision(work / "qt-declarative-and-speech.log",
              [*craft, "--use-cache", "--options", "[Compile]MakeProgram=nmake",
               "libs/qt/qtdeclarative", "libs/qt/qtspeech"], env, prefix)
    # jom rejects Poppler's generated assembly-output make rule.
    provision(work / "pdf-dependencies.log",
              [*craft, "--use-cache", "--options", "[Compile]MakeProgram=nmake",
               "qt-libs/poppler", "libs/libspectre"], env, prefix)
    provision(work / "okular-dependencies.log", [*craft, "--use-cache", "--install-deps", OKULAR], env, prefix)
    provision(work / "qt-and-kde-features.log", [*craft, "--use-cache", *FEATURE_PACKAGES], env, prefix)
    if any(prefix.rglob("ninja.exe")):
        raise ValueError("Craft provisioned Ninja despite the disabled blueprint. See the package logs.")

    replay["preflight"](prefix, cmake, env)
    packages, _ = replay["package_inventory"](prefix)
    inventory = [{"package": entry["name"], "version": entry["version"]} for entry in packages]
    (work / "installed-dependencies.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    if dependencies_only:
        print(f"Craft build dependencies are ready in {prefix}.")
        return

    env["PATH"] = os.pathsep.join(map(str, (prefix / "bin", prefix / "dev-utils/bin", env["PATH"])))
    env["QT_PLUGIN_PATH"] = str(prefix / "plugins")
    build_dir = work / "build"
    required = ("Poppler", "LibSpectre", "Qt6Qml", "Qt6TextToSpeech", "Qt6MultimediaWidgets", "KF6DocTools")
    run(work / "configure.log", [
        cmake, "-S", SOURCE, "-B", build_dir, "-G", "NMake Makefiles",
        f"-DCMAKE_MAKE_PROGRAM={prefix.as_posix()}/dev-utils/bin/jom.exe",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DBUILD_TESTING=ON",
        "-DBUILD_SHARED_LIBS=ON", "-DBUILD_WITH_QT6=ON",
        *(f"-DCMAKE_REQUIRE_FIND_PACKAGE_{name}=ON" for name in required),
    ], env)
    check_dependency_paths(build_dir / "CMakeCache.txt", prefix)
    run(work / "compile.log", [cmake, "--build", build_dir], env)
    run(work / "install.log", [cmake, "--install", build_dir], env)
    test_env = dict(env, QT_QPA_PLATFORM="offscreen")
    run(work / "ctest.log", [cmake.with_name("ctest.exe"), "--test-dir", build_dir, "--output-on-failure"], test_env)
    run(work / "shell.log", [build_dir / "bin/mainshelltest.exe", *replay["SHELL_CASES"],
                             "-o", f"{work / 'shell-results.txt'},txt"], test_env, cwd=build_dir)
    smoke_log = work / f"installed-pdf-{datetime.now(timezone.utc):%Y%m%d-%H%M%S-%f}.log"
    loaded = replay["smoke"](prefix, None, SOURCE / "autotests/data/simple-multipage.pdf", smoke_log, test_env)
    source = subprocess.run(["git", "-C", SOURCE, "rev-parse", "HEAD"],
                            env=env, capture_output=True, text=True)
    receipt = {
        "source_commit": source.stdout.strip() if source.returncode == 0 else None,
        **expected, "prefix": str(prefix),
        "executable": replay["identity"](prefix / "bin/okular.exe"),
        "pdf_recipe_patch": replay["identity"](SOURCE / "docs/craft-overrides/pdf-dependencies-2026-11-17.patch"),
        "pdf_modules_from_prefix": loaded,
    }
    (work / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"Okular compiled, installed and opened a PDF in {prefix}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--vcvars", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, help="Override the CMake installed by Craft")
    parser.add_argument("--dependencies-only", action="store_true")
    args = parser.parse_args()
    if sys.platform != "win32" or sys.version_info < (3, 13):
        raise ValueError("This utility requires Windows and Python 3.13 or newer.")
    import msvcrt

    work = args.work_dir.resolve()
    if work.is_relative_to(SOURCE) or SOURCE.is_relative_to(work):
        raise ValueError("Choose a build workspace separate from the Okular checkout.")
    work.mkdir(parents=True, exist_ok=True)
    with (work / ".public-build.lock").open("a+b") as lock:
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError(f"A build already holds the workspace lock in {work}") from error
        build(work, args.vcvars.resolve(), args.cmake.resolve() if args.cmake else None, args.dependencies_only)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, configparser.Error, sqlite3.Error, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
