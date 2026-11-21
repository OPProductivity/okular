#!/usr/bin/env python3
"""Capture/verify Craft recovery inputs and propose Qt build retirement. Never deletes.

Only generated Qt work/build directories are candidates. Installed files, source
trees, package images, the Okular build and local patch files are protected.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import zipfile

QT_MODULES = ("qtbase", "qtdeclarative", "qt5compat", "qtshadertools", "qttools",
              "qtlanguageserver", "qtsvg", "qtmultimedia", "qtspeech")
METADATA_NAMES = {"CMakeCache.txt", "config.summary", "config.redo.bat",
                  "install_manifest.txt", "compile_commands.json", "build.ninja"}
SAFE_UNTRACKED = {".patch", ".diff", ".py", ".cmake", ".md", ".txt"}
MUTABLE_RUNTIME_EXCLUSIONS = {"etc/PristineCraftenv.json"}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def inside(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def is_reparse(path: Path) -> bool:
    return bool(getattr(path.lstat(), "st_file_attributes", 0) & 0x400) or path.is_symlink()


def files(root: Path, strict: bool = False):
    """Do not traverse reparse directories or files, including junction aliases."""
    if is_reparse(root):
        raise ValueError(f"Reparse root: {root}")
    for current, dirs, names in os.walk(root, followlinks=False):
        plain = []
        for name in dirs:
            path = Path(current) / name
            if is_reparse(path):
                if strict:
                    raise ValueError(f"Reparse entry in protected tree: {path}")
            else:
                plain.append(name)
        dirs[:] = sorted(plain)
        for name in sorted(names):
            path = Path(current) / name
            if is_reparse(path):
                if strict:
                    raise ValueError(f"Reparse entry in protected tree: {path}")
                continue
            yield path


def runtime_files(craft: Path):
    """Hash installed inputs without descending into builds or Git checkouts."""
    excluded_roots = {"build", "download", "craft", "msys"}
    for current, dirs, names in os.walk(craft, followlinks=False):
        rel = Path(current).relative_to(craft)
        plain = []
        for name in dirs:
            path = Path(current) / name
            if is_reparse(path) or name == ".git":
                continue
            if not rel.parts and name.casefold() in excluded_roots:
                continue
            if rel.as_posix().casefold() == "etc/blueprints" and name.casefold() == "locations":
                continue  # Git bundle/patch capture this source checkout instead.
            plain.append(name)
        dirs[:] = sorted(plain)
        for name in sorted(names):
            path = Path(current) / name
            if (name != ".git" and not is_reparse(path) and
                    path.relative_to(craft).as_posix() not in MUTABLE_RUNTIME_EXCLUSIONS):
                yield path


def allocated(path: Path, logical: int) -> int:
    if os.name != "nt":
        return getattr(path.stat(), "st_blocks", 0) * 512
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    fn = kernel.GetCompressedFileSizeW
    fn.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
    fn.restype = ctypes.c_ulong
    high = ctypes.c_ulong()
    ctypes.set_last_error(0)
    low = fn(str(path), ctypes.byref(high))
    if low == 0xffffffff and ctypes.get_last_error():
        raise OSError(ctypes.get_last_error(), f"Cannot measure allocation: {path}")
    value = (high.value << 32) | low
    return ((value + 4095) // 4096) * 4096


def record(path: Path, root: Path, content_hash: bool = True) -> dict:
    stat = path.stat()
    result = {"path": path.relative_to(root).as_posix(), "bytes": stat.st_size,
              "mtime_ns": stat.st_mtime_ns, "identity": f"{stat.st_dev}:{stat.st_ino}",
              "links": stat.st_nlink, "allocated_estimate": allocated(path, stat.st_size)}
    if content_hash:
        result["sha256"] = sha(path)
    return result


def candidate_paths(craft: Path) -> list[Path]:
    result = []
    for module in QT_MODULES:
        path = craft / "build" / "libs" / "qt6" / module / "work" / "build"
        if not path.exists():
            continue
        if not path.is_dir() or is_reparse(path) or not inside(path, craft):
            raise ValueError(f"Unsafe candidate root: {path}")
        # A source tree is never a build candidate even if misnamed.
        if (path / "CMakeLists.txt").exists() or not (path / "CMakeCache.txt").is_file():
            raise ValueError(f"Expected generated CMake build, not a source tree: {path}")
        result.append(path)
    return result


def run_git(root: Path, *args: str) -> bytes:
    env = dict(os.environ, GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0")
    result = subprocess.run(["git", "-C", str(root), *args], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def git_snapshot(root: Path, dest: Path) -> dict:
    dest.mkdir(parents=True)
    head = run_git(root, "rev-parse", "HEAD").decode().strip()
    patch = run_git(root, "diff", "--binary", "HEAD")
    status = run_git(root, "status", "--porcelain=v1", "-uall")
    (dest / "working-tree.patch").write_bytes(patch)
    (dest / "status.txt").write_bytes(status)
    (dest / "refs.txt").write_bytes(run_git(root, "show-ref", "--head"))
    run_git(root, "bundle", "create", str(dest / "history.bundle"), "--all", "HEAD")
    run_git(root, "bundle", "verify", str(dest / "history.bundle"))
    untracked = run_git(root, "ls-files", "--others", "--exclude-standard", "-z")
    kept, excluded = [], []
    with zipfile.ZipFile(dest / "untracked-patches.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for raw in untracked.split(b"\0"):
            if not raw:
                continue
            rel = os.fsdecode(raw)
            path = root / rel
            if not inside(path, root) or is_reparse(path):
                raise ValueError(f"Unsafe untracked path: {rel}")
            if path.suffix.lower() in SAFE_UNTRACKED:
                z.write(path, rel.replace("\\", "/"))
                kept.append({"path": rel, "sha256": sha(path), "bytes": path.stat().st_size})
            else:
                excluded.append(rel)
    if run_git(root, "rev-parse", "HEAD").decode().strip() != head or run_git(root, "diff", "--binary", "HEAD") != patch or run_git(root, "status", "--porcelain=v1", "-uall") != status:
        raise RuntimeError(f"Git source changed during capture: {root}")
    return {"root": str(root), "head": head, "untracked_preserved": kept,
            "untracked_excluded": excluded}


def candidate_references(craft: Path, candidates: list[Path]) -> list[dict]:
    refs = []
    needles = [str(p).replace("\\", "/").casefold() for p in candidates]
    for sub in ("lib/cmake", "lib/pkgconfig", "mkspecs"):
        root = craft / sub
        if not root.is_dir():
            continue
        for path in files(root):
            if path.suffix.lower() not in {".cmake", ".pc", ".prl", ".conf"}:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                normalized = line.replace("\\", "/").casefold()
                if any(needle in normalized for needle in needles):
                    refs.append({"path": str(path), "line": number, "text": line[:1000]})
    return refs


def capture(craft: Path, source: Path, output: Path) -> None:
    craft, source, output = craft.resolve(), source.resolve(), output.resolve()
    if output.exists():
        raise ValueError("Capture requires a new, absent output directory")
    if inside(output, craft) or inside(output, source):
        raise ValueError("Recovery evidence must be outside Craft and the source checkout")
    candidates = candidate_paths(craft)
    if not candidates:
        raise ValueError("No reviewed Qt build candidates found")
    output.mkdir(parents=True)
    summary = {"schema": 1, "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
               "craft_root": str(craft), "source_root": str(source),
               "operation": "capture-only; no deletion/recycling/installation", "candidates": [],
               "git": {}, "archives": {}, "protected_files": [], "acceptance": {}}
    try:
        for name, root in (("okular", source), ("craft", craft / "craft"),
                           ("blueprints", craft / "etc/blueprints/locations/craft-blueprints-kde")):
            print(f"Capturing Git recovery: {name}", flush=True)
            summary["git"][name] = git_snapshot(root, output / "git" / name)
        for path in (craft / "etc").glob("*.ini"):
            # Preserve config for local recovery; never print it or commit it publicly.
            dest = output / "settings" / path.name
            dest.parent.mkdir(exist_ok=True)
            dest.write_bytes(path.read_bytes())
        database = craft / "etc/blueprints/install.db"
        if database.exists():
            with sqlite3.connect(database.as_uri() + "?mode=ro", uri=True) as src:
                with sqlite3.connect(output / "settings/install.db") as dst:
                    src.backup(dst)
        with zipfile.ZipFile(output / "qt-reconstruction-inputs.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for candidate in candidates:
                print(f"Inventory and source preservation: {candidate.parent.parent.name}", flush=True)
                records = [record(p, craft, False) for p in files(candidate, strict=True)]
                rel = candidate.relative_to(craft).as_posix()
                (output / (candidate.parent.parent.name + "-build-files.json")).write_text(json.dumps(records), encoding="utf-8")
                cache = (candidate / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
                summary["candidates"].append({"path": str(candidate), "relative": rel,
                    "files": len(records), "logical_bytes": sum(r["bytes"] for r in records),
                    "allocated_estimate": sum(r["allocated_estimate"] for r in records),
                    "index": candidate.parent.parent.name + "-build-files.json",
                    "cmake_identity": [l for l in cache.splitlines() if l.startswith(("CMAKE_GENERATOR:", "CMAKE_MAKE_PROGRAM:", "CMAKE_HOME_DIRECTORY:", "CMAKE_INSTALL_PREFIX:"))]})
                for path in files(candidate, strict=True):
                    if path.name in METADATA_NAMES or path.suffix.lower() == ".cmake":
                        z.write(path, path.relative_to(craft).as_posix())
                sources = [p for p in candidate.parent.iterdir() if p.is_dir() and "everywhere-src" in p.name]
                if not sources:
                    raise ValueError(f"No retained Qt source found for {candidate}")
                for root in sources:
                    for path in files(root, strict=True):
                        payload = path.read_bytes()
                        r = record(path, craft, False)
                        r["sha256"] = hashlib.sha256(payload).hexdigest()
                        summary["protected_files"].append(r)
                        z.writestr(path.relative_to(craft).as_posix(), payload)
        print("Hashing protected installed prefix (build directories excluded)", flush=True)
        for path in runtime_files(craft):
            rel = path.relative_to(craft)
            # DB and mutable maintenance logs are captured separately, not stable runtime inputs.
            if rel.as_posix() in {"etc/blueprints/install.db", "etc/cache.pickle"} or path.suffix.lower() in {".log", ".pyc"}:
                continue
            summary["protected_files"].append(record(path, craft))
        summary["installed_candidate_references"] = candidate_references(craft, candidates)
        for path in files(output):
            summary["archives"][path.relative_to(output).as_posix()] = {"bytes": path.stat().st_size, "sha256": sha(path)}
        summary["complete"] = True
    finally:
        (output / "recovery.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Recovery captured at {output}; run verify and a fresh app configure before approval.", flush=True)


def verify(snapshot: Path) -> dict:
    state = json.loads((snapshot / "recovery.json").read_text(encoding="utf-8"))
    if not state.get("complete"):
        raise ValueError("Incomplete capture cannot authorize retirement")
    failures = []
    for rel, rec in state["archives"].items():
        path = snapshot / rel
        if not inside(path, snapshot) or not path.is_file() or sha(path) != rec["sha256"]:
            failures.append(f"Recovery artifact mismatch: {rel}")
    source_records = {rec["path"]: rec for rec in state["protected_files"]
                      if "-everywhere-src-" in rec["path"] and rec["path"].startswith("build/libs/qt6/")}
    if source_records:
        archive = snapshot / "qt-reconstruction-inputs.zip"
        if not archive.is_file():
            failures.append("Missing Qt source archive")
        else:
            try:
                with zipfile.ZipFile(archive) as z:
                    names = z.namelist()
                    if len(names) != len(set(names)):
                        failures.append("Duplicate recovery archive member")
                    source_names = {name for name in names if name.startswith("build/libs/qt6/") and "-everywhere-src-" in name}
                    if source_names != source_records.keys():
                        failures.append("Qt source archive member inventory mismatch")
                    for name, rec in source_records.items():
                        if name not in source_names:
                            continue
                        h = hashlib.sha256()
                        with z.open(name) as stream:
                            for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b""):
                                h.update(chunk)
                        if h.hexdigest() != rec["sha256"]:
                            failures.append(f"Qt source archive member hash mismatch: {name}")
            except (OSError, zipfile.BadZipFile, KeyError) as error:
                failures.append(f"Invalid Qt source archive: {error}")
    craft = Path(state["craft_root"])
    excluded_mutable = []
    for rec in state["protected_files"]:
        if rec["path"] in MUTABLE_RUNTIME_EXCLUSIONS:
            # Older captures included this generated, session-specific environment cache.
            excluded_mutable.append(rec["path"])
            continue
        path = craft / rec["path"]
        if not inside(path, craft) or not path.is_file() or sha(path) != rec["sha256"]:
            failures.append(f"Protected input mismatch: {rec['path']}")
    for name, rec in state["git"].items():
        try:
            run_git(Path(rec["root"]), "bundle", "verify", str(snapshot / "git" / name / "history.bundle"))
        except RuntimeError as error:
            failures.append(str(error))
    if state["installed_candidate_references"]:
        failures.append("Installed configuration references a candidate directory")
    if failures:
        raise ValueError("\n".join(failures[:30]))
    result = {"checked_at": dt.datetime.now(dt.timezone.utc).isoformat(), "result": "pass",
              "protected_files": len(state["protected_files"]) - len(excluded_mutable),
              "excluded_mutable": excluded_mutable, "artifacts": len(state["archives"]),
              "source_members": len(source_records),
              "scope": "recovery artifacts, retained Qt sources, installed-prefix hashes; not a fresh Qt rebuild"}
    (snapshot / "verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result


def developer_environment(vcvars: Path, helper_dir: Path) -> dict:
    if not vcvars.is_file() or any(c in str(vcvars) for c in '\"&|<>^%!\r\n'):
        raise ValueError("Invalid vcvars64.bat path")
    helper_dir.mkdir(parents=True, exist_ok=True)
    helper = helper_dir / "collect-msvc-environment.cmd"
    helper.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b %errorlevel%\nset\n', encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(helper)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    # Windows environment variable names are case-insensitive. Keeping both
    # PATH from the caller and Path from vcvars makes CreateProcess choose an
    # unpredictable search path, hiding build-time DLLs from tools such as rcc.
    env = {key.upper(): value for key, value in os.environ.items()}
    # Environment is used in memory only; never log potential secret values.
    for line in result.stdout.decode(errors="replace").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key.upper()] = value
    return env


def probe(snapshot: Path, build: Path, cmake: Path, vcvars: Path, compile_app: bool) -> None:
    state = json.loads((snapshot / "recovery.json").read_text(encoding="utf-8"))
    if not state.get("complete"):
        raise ValueError("Incomplete snapshot")
    craft = Path(state["craft_root"])
    if build.exists() or inside(build, craft) or inside(build, Path(state["source_root"])):
        raise ValueError("Use a new probe build directory outside Craft and source")
    env = developer_environment(vcvars, snapshot)
    env["PATH"] = str(craft / "bin") + os.pathsep + env.get("PATH", "")
    command = [str(cmake), "-S", state["source_root"], "-B", str(build), "-G", "NMake Makefiles",
               "-DCMAKE_MAKE_PROGRAM=" + str(craft / "dev-utils/bin/jom.exe"),
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DCMAKE_INSTALL_PREFIX=" + str(craft),
               "-DCMAKE_PREFIX_PATH=" + str(craft), "-DBUILD_TESTING=ON", "-DBUILD_SHARED_LIBS=ON",
               "-DBUILD_WITH_QT6=ON"]
    commands = [command]
    if compile_app:
        commands.append([str(cmake), "--build", str(build), "--target", "okular", "--parallel", "2"])
    result = {"build_root": str(build.resolve()), "commands": commands, "install_performed": False,
              "qt_dependencies_rebuilt": False, "steps": []}
    log = snapshot / "app-probe.log"
    with log.open("wb") as stream:
        for cmd in commands:
            proc = subprocess.run(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT)
            result["steps"].append({"command": cmd, "returncode": proc.returncode})
            if proc.returncode:
                result["result"] = "failed"
                (snapshot / "app-probe.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
                raise RuntimeError(f"App probe failed; inspect {log}")
    result["result"] = "pass"
    result["scope"] = "fresh app configure" + (" and okular target compilation" if compile_app else "") + "; candidates remain present"
    (snapshot / "app-probe.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


def probe_qt(snapshot: Path, build: Path, cmake: Path, vcvars: Path, module: str, compile_module: bool) -> None:
    """Demonstrate a cold Qt-module build from retained sources without installation.

    This standard no-tests/no-examples profile does not claim every historical
    feature flag or all Qt modules have been reproduced.
    """
    state = json.loads((snapshot / "recovery.json").read_text(encoding="utf-8"))
    if not state.get("complete") or module not in QT_MODULES:
        raise ValueError("Use a complete snapshot and a supported Qt module")
    craft = Path(state["craft_root"])
    if build.exists() or inside(build, craft) or inside(build, Path(state["source_root"])):
        raise ValueError("Use a new probe directory outside retained source/runtime")
    parent = craft / "build/libs/qt6" / module / "work"
    sources = [p for p in parent.iterdir() if p.is_dir() and "everywhere-src" in p.name]
    bases = [p for p in (craft / "build/libs/qt6/qtbase/work").iterdir() if p.is_dir() and "everywhere-src" in p.name]
    if len(sources) != 1 or len(bases) != 1:
        raise ValueError("Expected exactly one preserved module and Qt base source version")
    # Keep each probe's bootstrap script in its own new directory so concurrent
    # module probes cannot rewrite a shared helper while another is launching it.
    env = developer_environment(vcvars, build)
    runtime_paths = [craft / "bin", craft / "dev-utils/bin"]
    if module == "qtbase":
        # Build-time tools such as rcc must load the QtCore DLL produced by this
        # cold tree, along with third-party DLLs retained in Craft's bin dir.
        runtime_paths.insert(0, build / "bin")
    env["PATH"] = os.pathsep.join(map(str, runtime_paths)) + os.pathsep + env.get("PATH", "")
    # Qt Speech's generated NMake rules need Microsoft's nmake. Qt Declarative
    # also stalled in jom's large build graph before compiling any target.
    make_program = "nmake" if module in ("qtspeech", "qtdeclarative") else str(craft / "dev-utils/bin/jom.exe")
    configure = [str(cmake), "-S", str(sources[0]), "-B", str(build), "-G", "NMake Makefiles",
                 "-DCMAKE_MAKE_PROGRAM=" + make_program,
                 "-DCMAKE_PREFIX_PATH=" + str(craft), "-DCMAKE_INSTALL_PREFIX=" + str(craft),
                 "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                 "-DQT_BUILD_TESTS=OFF", "-DQT_BUILD_EXAMPLES=OFF"]
    if module != "qtbase":
        # Installed QtBuildInternals may still refer to the old Z: source tree.
        configure.append("-DQT_SOURCE_TREE=" + str(bases[0]))
    else:
        # Match the retained Craft QtBase recipe. Autodetection otherwise
        # silently switches from ICU and system libraries to bundled/SDK modes.
        configure += [
            "-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES=" + str(craft / "include"),
            "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF",
            "-DPKG_CONFIG_EXECUTABLE=" + str(craft / "bin/pkg-config.exe"),
            "-DFEATURE_pkg_config=ON",
            "-DFEATURE_system_sqlite=ON",
            "-DFEATURE_system_freetype=ON",
            "-DFEATURE_system_harfbuzz=ON",
            "-DFEATURE_openssl_linked=ON",
            "-DFEATURE_icu=ON",
            "-DFEATURE_winsdkicu=OFF",
            "-DFEATURE_dbus_linked=OFF",
        ]
    commands = [configure]
    if compile_module:
        # NMake-generated top-level Makefiles contain .NOTPARALLEL; CMake
        # ignores --parallel and jom /J cannot override that guard safely.
        commands.append([str(cmake), "--build", str(build)])
    receipt = {"module": module, "profile": "standard module; no tests/examples; not exact historical feature parity",
               "install_performed": False, "build_root": str(build.resolve()), "steps": []}
    with (snapshot / f"qt-{module}-probe.log").open("wb") as stream:
        for command in commands:
            proc = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
            receipt["steps"].append({"command": command, "returncode": proc.returncode})
            if proc.returncode:
                break
    receipt["result"] = "pass" if all(r["returncode"] == 0 for r in receipt["steps"]) else "failed"
    (snapshot / f"qt-{module}-probe.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    if receipt["result"] != "pass":
        raise RuntimeError("Qt module probe failed; inspect its receipt/log")
    print(json.dumps(receipt, indent=2))


def write_plan(snapshot: Path, output: Path) -> None:
    state = json.loads((snapshot / "recovery.json").read_text(encoding="utf-8"))
    if not state.get("complete"):
        raise ValueError("Incomplete snapshot")
    verify(snapshot)
    if inside(output, Path(state["craft_root"])) or inside(output, Path(state["source_root"])):
        raise ValueError("Plan output must remain outside Craft and source")
    craft = Path(state["craft_root"])
    for candidate in state["candidates"]:
        expected = {rec["path"]: rec for rec in json.loads((snapshot / candidate["index"]).read_text(encoding="utf-8"))}
        current = {}
        for path in files(Path(candidate["path"]), strict=True):
            rec = record(path, craft, False)
            current[rec["path"]] = rec
        if current.keys() != expected.keys() or any(
            any(current[name][key] != expected[name][key] for key in ("bytes", "mtime_ns", "identity", "links"))
            for name in current.keys() & expected.keys()
        ):
            raise ValueError(f"Candidate inventory changed since capture: {candidate['path']}")
    lines = ["# Craft Qt build retirement plan", "", "PLAN ONLY. No move/delete command is provided.", "",
             "Only the nine named generated work/build trees below may be considered. Never select build/libs as a whole.", "",
             "| Exact original path | Files | Logical GiB | Allocated estimate GiB |", "| --- | ---: | ---: | ---: |"]
    for rec in state["candidates"]:
        lines.append(f"| {rec['path']} | {rec['files']} | {rec['logical_bytes']/2**30:.3f} | {rec['allocated_estimate']/2**30:.3f} |")
    lines += ["", "Keep the installed Craft prefix, all source trees, staging/debug images, source/Git patches, toolchains, package database, Qt base NMake diagnostic tree, and the entire current Okular build.", "",
              "## Recovery and five-round preservation", "",
              "1. Keep current source and current Okular incremental build for app changes and shell tests.",
              "2. Keep installed Qt/KF/PDF headers, libraries, plugins, tools and debug staging images.",
              "3. Keep all nine patched Qt source trees and the verified reconstruction archive, CMake caches/configure summaries and exact package/Git state.",
              "4. Keep Craft and blueprint bundles plus dirty patches/untracked patch files; do not promote historical Ninja/Z: cache settings as current instructions.",
              "5. Require fresh app configure/compile evidence, protected-input recheck, no active consumer/build, and exact matching Recycle Bin metadata/payload verification for any later explicitly approved move.", "",
              "The next five ordinary Okular source/test rounds use the retained installed dependencies and current app build. A Qt dependency change is a distinct cold rebuild and is not claimed instantaneous or freshly qualified here.", "",
              "Cold recovery: restore a missing retained source from qt-reconstruction-inputs.zip into a NEW staging path; restore Craft repositories from the bundles, then apply working-tree.patch and the preserved untracked patches. Verify archive hashes first. Reconstruct a Qt dependency through a reviewed current NMake/jom recipe, not a copied historical Ninja build cache. The installed prefix is retained, so ordinary app rebuilds do not require rebuilding Qt.", "",
              "## Mandatory execution gates (still no authorization)", "",
              "Run verify again; confirm the app probe passed; resolve every installed candidate reference; check current build/executable/working-directory consumers and reparse paths; compare each file inventory against the exact target; move ONLY approved targets through native SendToRecycleBin without a permanent fallback; stop on any failed move or failed bin verification. Preserve all recovery files outside every candidate. Record exact $I original-path and $R payload matches plus post-move protected hashes. Do not empty the bin.", "",
              "Same-volume recycling frees zero payload space immediately. Allocation is an estimate and hardlinks can reduce exclusive savings.", "",
              f"Recovery snapshot: {snapshot.resolve()}", f"Installed candidate references: {len(state['installed_candidate_references'])}",
              "Fresh Qt dependency rebuild after removing candidates has NOT been tested. Sources, metadata and runtime retention are the reconstruction boundary."]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    cap = commands.add_parser("capture")
    cap.add_argument("--craft-root", type=Path, required=True)
    cap.add_argument("--source-root", type=Path, required=True)
    cap.add_argument("--output", type=Path, required=True)
    ver = commands.add_parser("verify"); ver.add_argument("--snapshot", type=Path, required=True)
    plan = commands.add_parser("plan"); plan.add_argument("--snapshot", type=Path, required=True); plan.add_argument("--output", type=Path, required=True)
    pro = commands.add_parser("probe"); pro.add_argument("--snapshot", type=Path, required=True); pro.add_argument("--build-dir", type=Path, required=True)
    pro.add_argument("--cmake", type=Path, default=Path(r"C:\Program Files\CMake\bin\cmake.exe"))
    pro.add_argument("--vcvars", type=Path, default=Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"))
    pro.add_argument("--compile", action="store_true", help="Compile the Okular target; never install or launch it")
    qt = commands.add_parser("probe-qt", help="Cold Qt module probe; no installation or launch")
    qt.add_argument("--snapshot", type=Path, required=True); qt.add_argument("--build-dir", type=Path, required=True)
    qt.add_argument("--module", choices=QT_MODULES, default="qtsvg")
    qt.add_argument("--cmake", type=Path, default=Path(r"C:\Program Files\CMake\bin\cmake.exe"))
    qt.add_argument("--vcvars", type=Path, default=Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"))
    qt.add_argument("--compile", action="store_true")
    args = parser.parse_args()
    if args.command == "capture": capture(args.craft_root, args.source_root, args.output)
    elif args.command == "verify": print(json.dumps(verify(args.snapshot), indent=2))
    elif args.command == "plan": write_plan(args.snapshot, args.output)
    elif args.command == "probe": probe(args.snapshot, args.build_dir, args.cmake, args.vcvars, args.compile)
    elif args.command == "probe-qt": probe_qt(args.snapshot, args.build_dir, args.cmake, args.vcvars, args.module, args.compile)


if __name__ == "__main__":
    main()
