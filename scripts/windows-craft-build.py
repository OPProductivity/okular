#!/usr/bin/env python3
# SPDX-FileCopyrightText: None
# SPDX-License-Identifier: CC0-1.0
"""Provision a new Windows Craft prefix from public sources and build this checkout."""

from __future__ import annotations

import argparse
import configparser
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from urllib.error import URLError
from urllib.request import urlopen
import venv


CRAFT = "cae7aeac791e170c157eed18bb037e1fad8edf57"
BLUEPRINTS = "bdeeed04c1ef40770049b9d0cadba99bface4221"
ABI = "windows-cl-msvc2022-x86_64"
SOURCE = Path(__file__).resolve().parents[1]
GUIDE = "https://github.com/OPProductivity/okular/blob/master/docs/windows-craft-build.md#automated-public-build"
OKULAR = "kde/applications/okular"
NMAKE_OPTIONS = ("--options", "[Compile]MakeProgram=nmake")
FEATURE_PACKAGES = (
    "libs/qt/qtsvg", "libs/qt/qtmultimedia",
    "kde/frameworks/tier2/kdoctools", "kde/frameworks/tier2/kcolorscheme",
)
PROFILE = {"format": 1, "abi": ABI, "craft": CRAFT, "blueprints": BLUEPRINTS}
POPPLER_PACKAGE = "qt-libs/poppler"
GIB = 1024 ** 3
DEFAULT_FREE_GIB = 25
MSVC_DOWNLOAD = "https://aka.ms/vs/17/release/vs_BuildTools.exe"
SDK_DOWNLOAD = "https://developer.microsoft.com/windows/downloads/windows-sdk/"
FEEDS = {
    "KDE Craft": "https://github.com/KDE/craft",
    "KDE blueprints": "https://github.com/KDE/craft-blueprints-kde",
    "KDE MSVC cache": "https://files.kde.org/craft/Qt6/26.10/windows/cl/msvc2022/x86_64/RelWithDebInfo/manifest.json",
    "KDE GNU tools cache": "https://files.kde.org/craft/prebuilt/26.10/windows/mingw/x86_64/RelWithDebInfo/manifest.json",
    "PyPI certificates": "https://pypi.org/simple/certifi/",
    "MSYS2 packages": "https://repo.msys2.org/msys/x86_64/",
    "Poppler source": "https://invent.kde.org/mirrors/poppler",
}


def read_json(path: Path, default=None):
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else default


def write_json(path: Path, value):
    # Keep the previous complete file if the process stops while writing.
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_ini(path, settings):
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        settings.write(output)
    temporary.replace(path)


def find_vcvars() -> Path:
    installer = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
    executable = str(vswhere) if vswhere.is_file() else shutil.which("vswhere.exe")
    if executable:
        result = subprocess.check_output([
            executable, "-products", "*", "-version", "[17.0,18.0)",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath", "-utf8",
        ], encoding="utf-8")
        for line in result.splitlines():
            if line.strip():
                path = Path(line.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
                if path.is_file():
                    return path.resolve()
    raise ValueError(f"Visual Studio 2022 x64 C++ tools were not found. Install the v143 x64/x86 build tools "
                     f"and a Windows SDK ({MSVC_DOWNLOAD}), or specify --vcvars. See {GUIDE}")


def check_feeds():
    def probe(url):
        try:
            with urlopen(url, timeout=10) as response:
                response.read(1)
                return f"HTTP {response.status}"
        except (OSError, URLError) as error:
            return f"unconfirmed ({error})"

    with ThreadPoolExecutor(max_workers=len(FEEDS)) as executor:
        return dict(zip(FEEDS, executor.map(probe, FEEDS.values())))


def host_preflight(work: Path, vcvars: Path, min_free_gib: float, probe_feeds: bool):
    free = shutil.disk_usage(work).free
    print(f"Free on {work.anchor}: {free / GIB:.1f} GiB", flush=True)
    if free < min_free_gib * GIB:
        raise ValueError(f"This run needs {min_free_gib:g} GiB of free disk space under {work.anchor}. "
                         "Use another build drive or set --min-free-gib for a smaller resumed build.")
    env = environment(vcvars, work)
    sdk_version = env.get("WINDOWSSDKVERSION", "").strip("\\/")
    sdk = Path(env.get("WINDOWSSDKDIR", ""))
    if (not sdk.is_absolute() or not sdk_version
            or not all((sdk / "Include" / sdk_version / name).is_file()
                       for name in ("um/Windows.h", "ucrt/stdio.h"))
            or not all((sdk / "Lib" / sdk_version / name).is_file()
                       for name in ("um/x64/kernel32.lib", "ucrt/x64/ucrt.lib"))):
        raise ValueError(f"Install the x64 Windows SDK headers and libraries in Visual Studio Installer. {SDK_DOWNLOAD}")
    with tempfile.TemporaryDirectory(prefix="host-check-", dir=work) as temp:
        temp = Path(temp)
        source = temp / "check.cpp"
        source.write_text("#include <windows.h>\n#include <cstdio>\nint main() { return 0; }\n", encoding="ascii")
        compiler = shutil.which("cl.exe", path=env["PATH"])
        run(work / "host-preflight.log", [compiler, "/nologo", "/EHsc", "/Fe:check.exe", source],
            env, cwd=temp)
    host = {
        "vcvars": str(vcvars), "msvc": env.get("VCTOOLSVERSION"), "sdk": sdk_version,
        "sdk_path": str(sdk), "python": sys.version.split()[0], "python_executable": sys.executable,
        "git": subprocess.check_output(["git.exe", "--version"], env=env, text=True).strip(),
        "compiler": compiler,
    }
    print(f"MSVC {host['msvc']} x64, Windows SDK {sdk_version}, Python {host['python']}", flush=True)
    atl = Path(env["VCTOOLSINSTALLDIR"]) / "atlmfc/include/atlbase.h"
    if not atl.is_file():
        print("ATL headers are missing. Install v143 ATL for a Qt Speech SAPI source build.", flush=True)
    if probe_feeds:
        for name, result in check_feeds().items():
            print(f"{name}: {result} — {FEEDS[name]}", flush=True)
    return env, host


def run(log: Path, argv, env, *, cwd=None):
    print(f"{log.stem}: {log}", flush=True)
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"\n> {subprocess.list2cmdline(list(map(str, argv)))}\n")
        stream.flush()
        result = subprocess.run(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=stream, stderr=subprocess.STDOUT)
        stream.write(f"\n[exit status {result.returncode}]\n")
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
    settings["Compile"]["MakeProgram"] = "nmake"
    settings["ShortPath"]["JunctionDir"] = str(work / "short")
    settings["BlueprintVersions"]["EnableDailyUpdates"] = "False"
    settings["Packager"]["RepositoryUrl"] = "https://files.kde.org/craft/Qt6/"
    etc = prefix / "etc"
    etc.mkdir(exist_ok=True)
    # pkgconf's Muon dependency otherwise pulls Ninja in even with UseNinja disabled.
    write_ini(etc / "CraftSettings.ini", settings)


def set_blueprint_options(prefix: Path):
    path = prefix / "etc/BlueprintSettings.ini"
    settings = configparser.ConfigParser(interpolation=None, allow_no_value=True)
    settings.optionxform = str
    settings.read(path, encoding="utf-8")
    changed = False
    for package, option, value in (("dev-utils/ninja", "ignored", "True"),
                                   (POPPLER_PACKAGE, "buildTests", "False")):
        if settings.get(package, option, fallback=None) != value:
            if not settings.has_section(package):
                settings.add_section(package)
            settings.set(package, option, value)
            changed = True
    if changed:
        write_ini(path, settings)


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
    if subprocess.run([python, "-c", "import pip"], env=env,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        run(work / "pip.log", [python, "-m", "ensurepip", "--upgrade"], env)
    if subprocess.run([python, "-c", "import certifi"], env=env,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        run(work / "pip.log", [python, "-m", "pip", "--disable-pip-version-check", "install", "certifi"], env)

    craft = prefix / "craft/bin/craft.py"
    tree = prefix / "craft-tmp"
    if not (prefix / "etc/CraftSettings.ini").is_file():
        checkout(work, tree, "craft", CRAFT, env)
        initialize_settings(work, prefix, tree, python)
    set_blueprint_options(prefix)
    if not (work / "bootstrap-complete").is_file() or not craft.is_file():
        entry = craft if craft.is_file() else tree / "bin/craft.py"
        if not entry.is_file():
            raise ValueError(f"Incomplete Craft bootstrap in {prefix}. See {work / 'bootstrap.log'}.")
        provision(work / "bootstrap.log", [python, "-u", entry, *NMAKE_OPTIONS, "craft"], env, prefix)
    if not craft.is_file():
        raise ValueError(f"Craft did not install its command-line entry point under {prefix}")

    checkout(work, prefix / "craft", "craft", CRAFT, env)
    blueprints = prefix / "etc/blueprints/locations/craft-blueprints-kde"
    checkout(work, blueprints, "craft-blueprints-kde", BLUEPRINTS, env)
    apply_pdf_recipes(work, blueprints, env)
    prefer_msys_primary(prefix)
    (work / "bootstrap-complete").touch()
    return {"log": "bootstrap.log"}


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
    if (env.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x64"
            or not env.get("VSCMD_VER", "").startswith("17.")):
        raise ValueError("Use the Visual Studio 2022 x64 vcvars64.bat.")
    required_groups = (
        (("git.exe",), "Git for Windows", "https://git-scm.com/install/windows"),
        (("cl.exe", "link.exe", "nmake.exe"), "v143 x64/x86 C++ build tools", MSVC_DOWNLOAD),
        (("rc.exe", "mt.exe"), "Windows SDK", SDK_DOWNLOAD),
    )
    for tools, component, url in required_groups:
        missing = [tool for tool in tools if not shutil.which(tool, path=env["PATH"])]
        if missing:
            raise ValueError(f"Missing {', '.join(missing)}. Install {component} ({url}). See {GUIDE}")
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


def compile_okular(work: Path, cmake: Path, build_dir: Path, env) -> str:
    log = work / "compile.log"
    start = log.stat().st_size if log.exists() else 0
    try:
        run(log, [cmake, "--build", build_dir], env)
    except ValueError:
        with log.open("rb") as stream:
            stream.seek(start)
            output = stream.read()
        if b"jom:" not in output or b"Error: syntax error in" not in output:
            raise
        print("Retrying the generated NMake makefiles with Microsoft NMake.", flush=True)
        nmake = shutil.which("nmake.exe", path=env["PATH"])
        run(work / "compile-nmake.log", [nmake, "/NOLOGO"], env, cwd=build_dir)
        return "nmake"
    return "jom"


def with_log(log, action):
    action()
    return {"log": log.name}


def repair_package_outputs(prefix, files, replay, craft, env, log):
    missing = [path for path in files if not path.is_file()]
    if not missing:
        return
    _, owners = replay["package_inventory"](prefix)
    packages = set()
    for path in missing:
        names = owners.get(path.relative_to(prefix).as_posix().casefold(), {})
        packages.update(names)
    if packages:
        provision(log, [*craft, "--use-cache", "--ignoreInstalled", *sorted(packages)], env, prefix)


class Stages:
    def __init__(self, work):
        self.work = work
        self.path = work / "build-stages.json"
        self.records = read_json(self.path, {})

    def outputs(self, paths):
        return {path.relative_to(self.work).as_posix(): file_digest(path) for path in paths if path.is_file()}

    def run(self, name, inputs, outputs, action):
        paths = outputs if callable(outputs) else lambda: outputs
        previous = self.records.get(name, {})
        expected = paths()
        current = self.outputs(expected)
        if (previous.get("status") == "passed" and previous.get("inputs") == inputs
                and len(current) == len(expected) and previous.get("outputs") == current):
            print(f"Verified {name} from the previous run ({previous['seconds']:.1f} s).", flush=True)
            return previous
        record = {
            "status": "running", "inputs": inputs, "started": datetime.now(timezone.utc).isoformat(),
            "attempt": previous.get("attempt", 0) + 1,
        }
        history = previous.get("history", [])
        if previous:
            history = [*history, {key: previous[key] for key in
                                  ("attempt", "status", "started", "finished", "seconds", "error")
                                  if key in previous}]
        if history:
            record["history"] = history
        self.records[name] = record
        write_json(self.path, self.records)
        started = time.monotonic()
        try:
            record["details"] = action()
            expected = paths()
            record["outputs"] = self.outputs(expected)
            if len(record["outputs"]) != len(expected):
                missing = [str(path) for path in expected if not path.is_file()]
                raise ValueError(f"{name} did not produce: {', '.join(missing)}")
        except BaseException as error:
            record.update(status="failed", error=str(error) or type(error).__name__)
            raise
        else:
            record["status"] = "passed"
        finally:
            record.update(seconds=round(time.monotonic() - started, 3), finished=datetime.now(timezone.utc).isoformat())
            write_json(self.path, self.records)
        print(f"{name} passed in {record['seconds']:.1f} s.", flush=True)
        return record


def source_inputs(env):
    digest = hashlib.sha256()
    # Git's tracked-file list works for a checkout nested in another workspace.
    # Include untracked files because a source edit can refer to a new local file.
    if (SOURCE / ".git").exists():
        output = subprocess.check_output(
            ["git", "-C", SOURCE, "ls-files", "-z", "--cached", "--others", "--exclude-standard"], env=env)
        names = sorted(set(os.fsdecode(output).split("\0")) - {""})
        paths = (SOURCE / name for name in names)
        commit = subprocess.check_output(["git", "-C", SOURCE, "rev-parse", "HEAD"], env=env, text=True).strip()
    else:
        paths = sorted(path for path in SOURCE.rglob("*") if path.is_file())
        commit = None
    for path in paths:
        digest.update(os.fsencode(path.relative_to(SOURCE).as_posix()) + b"\0")
        if path.is_file():
            digest.update(file_digest(path).encode("ascii"))
        digest.update(b"\0")
    return {"source_commit": commit, "source_tree_sha256": digest.hexdigest(), "source_dir": str(SOURCE)}


def package_routes(logs):
    routes = {}
    handling = re.compile(r"\*\*\* Handling package: ([\w/+.~-]+), action:")
    for log in logs:
        if not log.is_file():
            continue
        package = None
        lines = []

        def record_route():
            if not package:
                return
            output = "\n".join(lines)
            if f"Action: post-qmerge for {package}" in output and not re.search(r"\bFAILED\b|fatal error:", output):
                route = "Craft recipe"
            elif (f"Action: fetch-binary for {package}" in output
                  and (archive := re.search(r"^Extracting archive: (.+)", output, re.MULTILINE))
                  and f"{package} not found in cache" not in output
                  and not re.search(r"\bFAILED\b|fatal error:", output)):
                route = "Craft binary cache"
            else:
                return
            routes[package] = {"route": route, "log": log.name}
            if route == "Craft binary cache":
                routes[package]["archive"] = Path(archive[1].strip()).name

        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            match = handling.search(line)
            if match or line.startswith("> ") or line.startswith("[exit status"):
                record_route()
                package = match[1] if match else None
                lines = []
            elif package:
                lines.append(line)
        record_route()
    return routes


def configure_arguments(prefix, build_dir, cmake):
    required = ("Poppler", "LibSpectre", "Qt6Qml", "Qt6TextToSpeech", "Qt6MultimediaWidgets", "KF6DocTools")
    return [
        str(cmake), "-S", str(SOURCE), "-B", str(build_dir), "-G", "NMake Makefiles",
        f"-DCMAKE_MAKE_PROGRAM={prefix.as_posix()}/dev-utils/bin/jom.exe",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DBUILD_TESTING=ON",
        "-DBUILD_SHARED_LIBS=ON", "-DBUILD_WITH_QT6=ON",
        *(f"-DCMAKE_REQUIRE_FIND_PACKAGE_{name}=ON" for name in required),
    ]


def owned_workspace(work, *, create):
    marker = work / "public-build.json"
    if marker.exists():
        if read_json(marker) != PROFILE:
            raise ValueError("This workspace belongs to another Craft profile. Choose a new workspace.")
    elif create and not any(entry.name not in (".public-build.lock", ".public-build.json.tmp")
                            for entry in work.iterdir()):
        write_json(marker, PROFILE)
    else:
        raise ValueError("Use an empty workspace for the first build, or select an existing public build workspace.")


def build(work: Path, vcvars: Path, cmake: Path | None, args):
    owned_workspace(work, create=True)
    prefix = work / "prefix"
    stages = Stages(work)
    cold = not (prefix / "etc/CraftSettings.ini").is_file()
    floor = args.min_free_gib if args.min_free_gib is not None else (DEFAULT_FREE_GIB if cold else 0)
    env, host = host_preflight(work, vcvars, floor, not args.skip_feed_check)
    if args.preflight_only:
        return

    replay = runpy.run_path(str(SOURCE / "scripts/windows-craft-prefix.py"))
    if cmake:
        replay["require_files"](cmake.parent, (cmake.name, "ctest.exe"))
    patch_dir = SOURCE / "docs/craft-overrides"
    recipes = {name: file_digest(patch_dir / name) for name in (
        "pdf-dependencies-2026-11-17.patch", "libspectre-0.2.12-windows.diff")}
    python = work / "bootstrap-python/Scripts/python.exe"
    craft_entry = prefix / "craft/bin/craft.py"
    blueprints = prefix / "etc/blueprints/locations/craft-blueprints-kde"
    craft = [python, "-u", craft_entry, *NMAKE_OPTIONS]
    bootstrap_record = stages.run(
        "bootstrap", {**PROFILE, "host": host, "recipes": recipes, "poppler_build_tests": False},
        [python, craft_entry, prefix / "etc/CraftSettings.ini", prefix / "craft/.git/HEAD",
         blueprints / ".git/HEAD", blueprints / "qt-libs/poppler/poppler.py",
         blueprints / "libs/libspectre/libspectre-0.2.12-windows.diff"],
        lambda: bootstrap(work, prefix, env),
    )
    # Craft rewrites this INI as packages register options. Preserve their settings
    # while reasserting the two settings owned by the public build.
    set_blueprint_options(prefix)
    cmake = cmake or prefix / "dev-utils/bin/cmake.exe"
    repair_log = work / "dependency-repair.log"
    if cmake.is_relative_to(prefix):
        repair_package_outputs(prefix, (cmake, cmake.with_name("ctest.exe")), replay, craft, env, repair_log)
    replay["require_files"](cmake.parent, (cmake.name, "ctest.exe"))
    poppler_profile = work / "poppler-profile.json"
    dependency_inventory = work / "installed-dependencies.json"
    package_logs = [work / name for name in (
        "bootstrap.log", "qt-declarative-and-speech.log", "poppler.log", "pdf-dependencies.log",
        "okular-dependencies.log", "qt-and-kde-features.log", "dependency-repair.log")]
    poppler_outputs = [prefix / "bin" / name for name in ("poppler.dll", "poppler-qt6.dll")]

    def prepare_poppler():
        # An older workspace can contain this same version with Poppler's test
        # targets enabled. Craft would otherwise retain it as up to date.
        stamp = read_json(poppler_profile, {})
        override = []
        if any(path.exists() for path in poppler_outputs) and stamp.get("build_tests") is not False:
            override = ["--ignoreInstalled", "--no-cache"]
        elif poppler_profile.is_file():
            # This stage is rerunning after a recorded artifact changed or went
            # missing. Force qmerge even if Craft's package DB still says installed.
            override = ["--ignoreInstalled", "--use-cache"]
        else:
            override = ["--use-cache"]
        provision(package_logs[2], [*craft, *override, POPPLER_PACKAGE], env, prefix)
        write_json(poppler_profile, {"build_tests": False})
        return {"log": package_logs[2].name}

    # Speech is installed before PDF/KDE dependencies so its Qt Declarative
    # build and ABI are selected before a dependent package can choose them.
    qt_outputs = [prefix / "bin/Qt6Qml.dll", prefix / "bin/Qt6TextToSpeech.dll"]

    def prepare_qt():
        provision(package_logs[1], [*craft, "--use-cache", "libs/qt/qtdeclarative",
                                   "libs/qt/qtspeech"], env, prefix)
        repair_package_outputs(prefix, qt_outputs, replay, craft, env, repair_log)
        return {"log": package_logs[1].name}

    qt_record = stages.run(
        "qt-before-pdf", {"bootstrap": bootstrap_record["finished"]},
        qt_outputs, prepare_qt,
    )
    poppler_record = stages.run(
        "poppler", {"qt": qt_record["finished"], "recipes": recipes, "build_tests": False},
        [poppler_profile, *poppler_outputs], prepare_poppler,
    )

    dependency_files = [
        prefix / "dev-utils/bin/jom.exe",
        *(prefix / "bin" / name for name in ("Qt6Core.dll", "Qt6Svg.dll", "Qt6MultimediaWidgets.dll",
                                             "KF6ColorScheme.dll", "spectre.dll", "gsdll64.dll",
                                             "qtpaths.exe", "moc.exe", "rcc.exe", "uic.exe",
                                             "kconfig_compiler_kf6.exe", "meinproc6.exe", "pkg-config.exe")),
        prefix / "plugins/platforms/qoffscreen.dll", prefix / "plugins/platforms/qwindows.dll",
    ]

    def prepare_dependencies():
        provision(package_logs[3], [*craft, "--use-cache", "libs/libspectre"], env, prefix)
        provision(package_logs[4], [*craft, "--use-cache", "--install-deps", OKULAR], env, prefix)
        provision(package_logs[5], [*craft, "--use-cache", *FEATURE_PACKAGES], env, prefix)
        repair_package_outputs(prefix, dependency_files, replay, craft, env, repair_log)
        if any(prefix.rglob("ninja.exe")):
            raise ValueError("Craft provisioned Ninja despite the disabled blueprint. See the package logs.")
        replay["preflight"](prefix, cmake, env)
        packages, _ = replay["package_inventory"](prefix)
        routes = package_routes(package_logs)
        inventory = [{"package": entry["name"], "version": entry["version"],
                      "cache_version": entry["cache_version"], "revision": entry["revision"],
                      **routes.get(entry["name"], {"route": "unconfirmed"})}
                     for entry in packages]
        write_json(dependency_inventory, inventory)
        return {"packages": len(inventory), "logs": [path.name for path in package_logs]}

    dependency_outputs = [dependency_inventory, prefix / "etc/blueprints/install.db",
                          cmake, cmake.with_name("ctest.exe"), *dependency_files]
    # An explicitly supplied CMake can be outside the workspace.
    dependency_outputs = [path for path in dependency_outputs if path.is_relative_to(work)]
    dependency_record = stages.run(
        "dependencies", {"poppler": poppler_record["finished"], "features": list(FEATURE_PACKAGES),
                         "cmake": str(cmake), "cmake_sha256": file_digest(cmake)},
        dependency_outputs, prepare_dependencies,
    )
    replay["preflight"](prefix, cmake, env)
    if args.dependencies_only:
        print(f"Craft build dependencies are ready in {prefix}.")
        return

    env["PATH"] = os.pathsep.join(map(str, (prefix / "bin", prefix / "dev-utils/bin", env["PATH"])))
    env["QT_PLUGIN_PATH"] = str(prefix / "plugins")
    build_dir = work / "build"
    source = source_inputs(env)
    configure = configure_arguments(prefix, build_dir, cmake)
    cache = build_dir / "CMakeCache.txt"

    def configure_app():
        run(work / "configure.log", configure, env)
        check_dependency_paths(cache, prefix)
        return {"log": "configure.log", "cmake": subprocess.check_output(
            [cmake, "--version"], env=env, text=True).splitlines()[0]}

    configure_record = stages.run(
        "configure", {"dependencies": dependency_record["finished"], **source, "command": configure},
        [cache, build_dir / "Makefile", build_dir / "CTestTestfile.cmake"], configure_app,
    )
    check_dependency_paths(cache, prefix)

    def compiled_paths():
        bin_dir = build_dir / "bin"
        required = {bin_dir / name for name in ("okular.exe", "Okular6Core.dll", "okularpart.dll",
                                               "mainshelltest.exe", "okular_generators/okularGenerator_poppler.dll")}
        return sorted(required.union(bin_dir.rglob("*.exe"), bin_dir.rglob("*.dll")))

    compiled = stages.run(
        "compile", {"configure": configure_record["finished"]}, compiled_paths,
        lambda: {"make": compile_okular(work, cmake, build_dir, env), "log": "compile.log"},
    )
    manifest = build_dir / "install_manifest.txt"

    def install_app():
        run(work / "install.log", [cmake, "--install", build_dir], env)
        installed = replay["app_install_files"](prefix, manifest)
        replay["require_files"](prefix, installed)
        return {"files": len(installed), "log": "install.log"}

    def installed_paths():
        names = replay["app_install_files"](prefix, manifest) if manifest.is_file() else replay["KEY_FILES"]
        return [manifest, *(prefix / name for name in sorted(names))]

    # A missing nonessential installed artifact also triggers reinstall.
    installed = stages.run("install", {"compile": compiled["finished"]},
                           installed_paths, install_app)
    test_env = dict(env, QT_QPA_PLATFORM="offscreen")

    def test_app():
        run(work / "ctest.log", [cmake.with_name("ctest.exe"), "--test-dir", build_dir,
                                 "--output-on-failure", "--no-tests=error"], test_env)
        shutil.copyfile(build_dir / "Testing/Temporary/LastTest.log", work / "ctest-results.log")
        return {"log": "ctest.log"}

    stages.run(
        "ctest", {"install": installed["finished"]}, [work / "ctest-results.log"], test_app,
    )
    stages.run(
        "shell", {"install": installed["finished"], "cases": list(replay["SHELL_CASES"])},
        [work / "shell-results.txt"],
        lambda: with_log(work / "shell.log", lambda: run(
            work / "shell.log", [build_dir / "bin/mainshelltest.exe", *replay["SHELL_CASES"],
                                 "-o", f"{work / 'shell-results.txt'},txt"], test_env, cwd=build_dir)),
    )

    def smoke():
        smoke_log = work / f"installed-pdf-{datetime.now(timezone.utc):%Y%m%d-%H%M%S-%f}.log"
        loaded = replay["smoke"](prefix, None, SOURCE / "autotests/data/simple-multipage.pdf", smoke_log, test_env)
        write_json(work / "installed-pdf.json", {"modules_from_prefix": loaded, "log": smoke_log.name})
        return {"log": smoke_log.name}

    stages.run("installed-pdf", {"install": installed["finished"]}, [work / "installed-pdf.json"], smoke)
    receipt = {
        **source, **PROFILE, "prefix": str(prefix), "host": host, "recipes": recipes,
        "okular_make": compiled["details"]["make"], "craft_source_make": "nmake",
        "executable": replay["identity"](prefix / "bin/okular.exe"),
        "pdf_modules_from_prefix": read_json(work / "installed-pdf.json")["modules_from_prefix"],
        "dependency_inventory": dependency_inventory.name,
        "stages": {name: {key: record[key] for key in ("finished", "seconds", "attempt", "details")}
                   for name, record in stages.records.items() if record["status"] == "passed"},
    }
    write_json(work / "build-receipt.json", receipt)
    print(f"Okular compiled, installed and opened a PDF in {prefix}.")


def storage(work, recycle_output):
    owned_workspace(work, create=False)
    helper = runpy.run_path(str(SOURCE / "scripts/windows-craft-storage.py"))
    if not recycle_output:
        helper["disk_report"](work)
        return
    stages = Stages(work)
    install = stages.records.get("install", {})
    checks = ("ctest", "shell", "installed-pdf")
    required = [stages.records.get(name, {}) for name in checks]
    receipt = read_json(work / "build-receipt.json", {})
    finished_stages = receipt.get("stages", {})
    if (receipt.get("prefix") != str(work / "prefix") or install.get("status") != "passed"
            or any(record.get("status") != "passed" or record["inputs"]["install"] != install["finished"]
                   for record in required)
            or any(record.get("status") != "passed"
                   or finished_stages.get(name, {}).get("finished") != record["finished"]
                   for name, record in stages.records.items())):
        raise ValueError("Run the build through PDF startup verification before recycling generated output.")
    combined_outputs = dict(install["outputs"])
    for record in required:
        combined_outputs.update(record["outputs"])
    essential = {name: digest for name, digest in combined_outputs.items() if not name.startswith("build/")}
    if stages.outputs([work / name for name in essential]) != essential:
        raise ValueError("An installed app file or a test result changed. Rerun the build before recycling.")
    # Require the dependency runtime checked by the successful build too.
    for stage in ("poppler", "qt-before-pdf", "dependencies"):
        record = stages.records.get(stage, {})
        outputs = record.get("outputs", {})
        if record.get("status") != "passed" or stages.outputs([work / name for name in outputs]) != outputs:
            raise ValueError(f"{stage} outputs changed. Rerun the build before recycling.")
    moved = helper["recycle"](work)
    if moved:
        print("Windows will reclaim disk space when you empty those entries from the Recycle Bin.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--vcvars", type=Path, help="Override autodetection of Visual Studio 2022 vcvars64.bat")
    parser.add_argument("--cmake", type=Path, help="Override the CMake installed by Craft")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dependencies-only", action="store_true")
    mode.add_argument("--preflight-only", action="store_true")
    mode.add_argument("--disk-report", action="store_true", help="Report disk use for an existing public build")
    mode.add_argument("--recycle-build-output", action="store_true", help="Recycle verified generated build trees")
    parser.add_argument("--skip-feed-check", action="store_true", help="Skip the advisory network probes")
    parser.add_argument("--min-free-gib", type=float, help="Override the free-space check for this run")
    args = parser.parse_args()
    if sys.platform != "win32" or sys.version_info < (3, 13):
        raise ValueError("This utility requires Windows and Python 3.13 or newer.")
    if args.min_free_gib is not None and not 0 <= args.min_free_gib < float("inf"):
        parser.error("--min-free-gib must be a finite nonnegative number")
    import msvcrt

    requested = Path(os.path.abspath(args.work_dir))
    work = requested.resolve()
    if work.is_relative_to(SOURCE) or SOURCE.is_relative_to(work):
        raise ValueError("Choose a build workspace separate from the Okular checkout.")
    storage_mode = args.disk_report or args.recycle_build_output
    if storage_mode:
        if requested != work:
            raise ValueError("Give the direct path to the public build workspace.")
        owned_workspace(work, create=False)
    else:
        work.mkdir(parents=True, exist_ok=True)
    with (work / ".public-build.lock").open("a+b") as lock:
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError(f"A build already holds the workspace lock in {work}") from error
        if storage_mode:
            storage(work, args.recycle_build_output)
        else:
            build(work, args.vcvars.resolve() if args.vcvars else find_vcvars(),
                  args.cmake.resolve() if args.cmake else None, args)

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, configparser.Error, sqlite3.Error, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
