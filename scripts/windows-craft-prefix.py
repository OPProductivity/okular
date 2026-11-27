#!/usr/bin/env python3
"""Export private Craft inputs, then restore them to a new Windows prefix."""

from __future__ import annotations

import argparse
from collections import defaultdict
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import runpy
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import zipfile


APP = "kde/applications/okular"
GIB = 1024**3
PAYLOAD_ROOTS = frozenset("""
    bin certs cmake dev-utils doc etc html include lib libexec licenses metatypes
    mkspecs modules phrasebooks pkgconfig plugins qml sbin share site translations
""".split())
KEY_FILES = (
    "bin/okular.exe", "bin/Okular6Core.dll", "bin/okular-spectre-render.exe",
    "plugins/kf6/parts/okularpart.dll",
    "plugins/okular_generators/okularGenerator_poppler.dll",
    "plugins/okular_generators/okularGenerator_ghostview.dll",
)


def canonical_path(name: str) -> str:
    path = PurePosixPath(name.replace("\\", "/"))
    windows = PureWindowsPath(path.as_posix())
    if (not path.parts or path.is_absolute() or windows.drive or ".." in path.parts
            or any(":" in part or part.endswith((".", " ")) or os.path.isreserved(part)
                   for part in path.parts)):
        raise ValueError(f"Unsafe payload path: {name}")
    return path.as_posix()


def transfer(source, target=None) -> dict:
    digest = hashlib.sha256()
    size = 0
    for block in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(block)
        size += len(block)
        if target is not None:
            target.write(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def identity(path: Path) -> dict:
    with path.open("rb") as source:
        return transfer(source)


def package_inventory(craft: Path) -> tuple[list, dict]:
    uri = (craft / "etc/blueprints/install.db").as_uri() + "?mode=ro"
    owners = defaultdict(dict)
    with sqlite3.connect(uri, uri=True) as db:
        packages = [
            dict(name=name, version=version, cache_version=cache, revision=revision)
            for name, version, cache, revision in db.execute(
                "SELECT packagePath, version, cacheVersion, revision FROM packageList ORDER BY packagePath")
        ]
        for name, path, digest in db.execute(
                "SELECT packagePath, filename, fileHash FROM fileList JOIN packageList USING (packageId)"):
            path = canonical_path(path)
            owners[path.casefold()][name] = (path, digest)
    return packages, owners


def app_install_files(craft: Path, install_manifest: Path) -> set[str]:
    app_files = set()
    for line in install_manifest.read_text(encoding="utf-8").splitlines():
        if line:
            path = Path(line)
            app_files.add(canonical_path(path.relative_to(craft).as_posix()))
    provenance = {path.casefold() for path in app_files}
    if any(path.casefold() not in provenance for path in KEY_FILES):
        raise ValueError("The Okular install manifest lacks an essential installed component")
    return app_files


def capture(craft: Path, install_manifest: Path, output: Path):
    if output.is_relative_to(craft):
        raise ValueError("The private bundle must be outside the installed Craft prefix")
    packages, owners = package_inventory(craft)
    app_files = app_install_files(craft, install_manifest)
    app_names = {name.casefold() for name in app_files}
    selected = {"dependencies": {}, "installed-okular": {}}
    omitted = defaultdict(int)
    missing = []
    for key, records in owners.items():
        path = next(iter(records.values()))[0]
        if key in app_names or APP in records:
            kind = "installed-okular"
        elif key.split("/")[0] in PAYLOAD_ROOTS and not key.endswith(".pdb"):
            kind = "dependencies"
        else:
            omitted[key.split("/")[0] if not key.endswith(".pdb") else "debug symbols"] += 1
            continue
        if not (craft / path).is_file():
            missing.append({"path": path, "packages": sorted(records)})
            continue
        selected[kind][path.casefold()] = path
    for path in app_files:
        if not (craft / path).is_file():
            raise ValueError(f"Current Okular install file is missing: {path}")
        selected["installed-okular"][path.casefold()] = path

    size = 0
    for entries in selected.values():
        for name in entries.values():
            original = craft / name
            if original.resolve() != original or getattr(original.lstat(), "st_file_attributes", 0) & 0x400:
                raise ValueError(f"Payload path goes through a link or reparse point: {name}")
            size += original.stat().st_size
    free = shutil.disk_usage(output.parent).free
    # Allow for incompressible payloads, ZIP and filesystem overhead.
    if free < 1.05 * size + GIB:
        raise ValueError(f"Need {1.05 * size / GIB + 1:.2f} GiB free to capture; have {free / GIB:.2f} GiB")
    output.mkdir()
    lock = {"format": 1, "packages": packages, "missing_package_files": missing,
            "omitted_package_files": dict(omitted), "archives": {}}
    for kind, entries in selected.items():
        archive = output / f"{kind}.zip"
        members = {}
        with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as zipped:
            for key, name in sorted(entries.items()):
                with (craft / name).open("rb") as source, zipped.open(name, "w", force_zip64=True) as target:
                    members[name] = transfer(source, target)
                records = owners.get(key, {})
                members[name]["package_hashes"] = {
                    owner: digest for owner, (_, digest) in records.items()
                }
        lock["archives"][kind] = {"file": archive.name, **identity(archive), "members": members}
        print(f"{archive.name}: {len(members)} files, {archive.stat().st_size:,} bytes", flush=True)
    with (output / "payload.lock.json").open("x", encoding="utf-8") as out:
        json.dump(lock, out, indent=2)
        out.write("\n")


def verify_or_restore(bundle: Path, kind: str, destination: Path | None):
    lock_path = bundle / "payload.lock.json"
    archive = bundle / f"{kind}.zip"
    missing = [path.name for path in (lock_path, archive) if not path.is_file()]
    if missing:
        raise ValueError(
            f"Private Craft export missing from {bundle}: {', '.join(missing)}. "
            "For a build using only public inputs, follow docs/windows-craft-build.md."
        )
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if lock["format"] != 1:
        raise ValueError("Unsupported payload lock format")
    spec = lock["archives"][kind]
    if identity(archive) != {key: spec[key] for key in ("bytes", "sha256")}:
        raise ValueError(f"Archive checksum or size mismatch: {archive}")
    members = spec["members"]
    normalized = {canonical_path(name).casefold() for name in members}
    if len(normalized) != len(members) or any(canonical_path(name) != name for name in members):
        raise ValueError("Payload lock contains noncanonical or conflicting paths")
    with zipfile.ZipFile(archive) as zipped:
        infos = zipped.infolist()
        if set(zipped.namelist()) != members.keys() or len(infos) != len(members):
            raise ValueError("Archive membership does not match the lock")
        if any(info.file_size != members[info.filename]["bytes"] for info in infos):
            raise ValueError("Unexpected uncompressed file size")
        if destination is not None:
            needed = sum(info.file_size for info in infos)
            free = shutil.disk_usage(destination.parent).free
            if free < 1.05 * needed + GIB:
                raise ValueError(f"Need {1.05 * needed / GIB + 1:.2f} GiB free to restore; have {free / GIB:.2f} GiB")
            destination.mkdir()
        for info in infos:
            name = info.filename
            with zipped.open(info) as source:
                if destination is None:
                    actual = transfer(source)
                else:
                    target = destination / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open("xb") as out:
                        actual = transfer(source, out)
            if actual != {key: members[name][key] for key in ("bytes", "sha256")}:
                raise ValueError(f"Payload member checksum mismatch: {name}")
    print(f"{kind}: verified {len(members)} files")


def smoke(prefix: Path, original_prefix: Path, fixture: Path, log: Path, env: dict) -> list[str]:
    """Launch only the new install, with Qt's Windows test profile and IPC isolation."""
    test_env = dict(env, OKULAR_TEST_INSTANCE=f"craft-prefix-{os.getpid()}", QT_DEBUG_PLUGINS="1")
    powershell = Path(env["SYSTEMROOT"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    required = {
        "bin/Qt6Core.dll", "bin/Qt6TextToSpeech.dll", "bin/poppler.dll",
        "plugins/kf6/parts/okularpart.dll", "plugins/platforms/qoffscreen.dll",
        "plugins/okular_generators/okularGenerator_poppler.dll",
    }
    with log.open("xb") as output, subprocess.Popen(
        [prefix / "bin/okular.exe", fixture], cwd=log.parent, env=test_env,
        stdout=output, stderr=subprocess.STDOUT,
    ) as child:
        try:
            for _ in range(12):
                if child.poll() is not None:
                    raise ValueError(f"The installed reader exited during startup with status {child.returncode}")
                time.sleep(1)
                command = f"(Get-Process -Id {child.pid} -ErrorAction Stop).Modules | ForEach-Object FileName"
                result = subprocess.run([powershell, "-NoProfile", "-NonInteractive", "-Command", command],
                                        capture_output=True, text=True, check=True, timeout=10)
                modules = [Path(name) for name in result.stdout.splitlines() if name]
                if any(path.is_relative_to(original_prefix) for path in modules):
                    raise ValueError("The installed reader loaded a DLL from the original Craft prefix")
                local = sorted(path.relative_to(prefix).as_posix() for path in modules if path.is_relative_to(prefix))
                if set(map(str.casefold, required)).issubset(map(str.casefold, local)):
                    return local
            raise ValueError("The installed reader did not load the full required PDF stack")
        finally:
            if child.poll() is None:
                child.terminate()


def relocate_metadata(prefix: Path, original_prefix: Path) -> list[dict]:
    """Rewrite only dependency path records, leaving the verified ZIPs and binaries intact."""
    old_paths = (str(original_prefix), original_prefix.as_posix())
    pattern = re.compile(b"(?:" + b"|".join(re.escape(os.fsencode(path)) for path in old_paths)
                         + rb")(?=$|[/\\\"';\s])", re.IGNORECASE)
    replacement = prefix.as_posix().encode("utf-8")
    changes = []
    for root in ("cmake", "lib", "mkspecs", "pkgconfig", "share"):
        for suffix in ("cmake", "pc", "prl", "pri"):
            for path in sorted((prefix / root).rglob(f"*.{suffix}")):
                old = path.read_bytes()
                new, count = pattern.subn(lambda _: replacement, old)
                if count and new != old:
                    path.write_bytes(new)
                    changes.append({"path": path.relative_to(prefix).as_posix(),
                                    "before": hashlib.sha256(old).hexdigest(),
                                    "after": hashlib.sha256(new).hexdigest()})
    return changes


def require_files(root: Path, names):
    missing = [name for name in names if not (root / name).is_file()]
    if missing:
        raise ValueError(f"Missing files under {root}: " + ", ".join(missing))


def preflight(prefix: Path, cmake: Path, base: dict):
    if sys.version_info < (3, 13):
        raise ValueError("Use Python 3.13 or newer for the clean-prefix helper")
    require_files(cmake.parent, (cmake.name, "ctest.exe"))
    missing = [tool for tool in ("cl.exe", "link.exe", "rc.exe", "mt.exe", "nmake.exe")
               if not shutil.which(tool, path=base["PATH"])]
    if missing:
        raise ValueError("MSVC or Windows SDK tools missing from vcvars64.bat: " + ", ".join(missing))
    if base.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x64":
        raise ValueError("The developer environment must target x64")
    sdk = Path(base["WINDOWSSDKDIR"]) / "Include" / base["WINDOWSSDKVERSION"].strip("\\/")
    require_files(sdk, ("um/Windows.h",))
    require_files(prefix, (
        "dev-utils/bin/jom.exe", "bin/qtpaths.exe", "bin/moc.exe", "bin/rcc.exe", "bin/uic.exe",
        "bin/kconfig_compiler_kf6.exe", "bin/meinproc6.exe", "bin/pkg-config.exe",
        "bin/Qt6Core.dll", "bin/poppler-qt6.dll",
        "plugins/platforms/qoffscreen.dll", "plugins/platforms/qwindows.dll",
    ))
    atl = Path(base["VCTOOLSINSTALLDIR"]) / "atlmfc/include/atlbase.h"
    print(f"MSVC {base['VCTOOLSVERSION']} x64, Windows SDK {sdk.name}, Python {sys.version.split()[0]}")
    print(f"ATL headers for Qt Speech SAPI source builds: {'present' if atl.is_file() else 'missing'}")


def launch_preview(prefix: Path, documents: list[Path]):
    require_files(prefix, (*KEY_FILES, "plugins/platforms/qwindows.dll"))
    for document in documents:
        if not document.is_file():
            raise ValueError(f"File does not exist: {document}")
    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(("QT_", "QML_", "KDE_", "XDG_"))}
    windows = Path(os.environ["SystemRoot"])
    env["PATH"] = os.pathsep.join(map(str, (prefix / "bin", windows / "System32", windows)))
    env["QT_PLUGIN_PATH"] = str(prefix / "plugins")
    env["QT_QPA_PLATFORM"] = "windows"
    profile = hashlib.sha256(str(prefix).casefold().encode("utf-8")).hexdigest()[:16]
    env["OKULAR_TEST_INSTANCE"] = f"craft-preview-{profile}"
    app = subprocess.Popen([prefix / "bin/okular.exe", *documents], cwd=prefix / "bin", env=env,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Started preview process {app.pid} from {prefix}")


def build(source: Path, prefix: Path, build_dir: Path, cmake: Path, vcvars: Path,
          original_prefix: Path, commit: str):
    if (build_dir.exists() or build_dir.is_relative_to(prefix) or prefix.is_relative_to(build_dir)
            or any(path.is_relative_to(original_prefix) for path in (source, prefix, build_dir, cmake, vcvars))):
        raise ValueError("Use an absent build directory and an independent restored prefix")
    if subprocess.check_output(["git", "-C", source, "rev-parse", "HEAD"], text=True).strip() != commit:
        raise ValueError("Okular checkout does not match the requested source commit")
    if subprocess.check_output(["git", "-C", source, "status", "--porcelain"], text=True).strip():
        raise ValueError("Use a clean Okular source checkout")
    # Earlier versions of these tests wrote fixture metadata to the normal DocData directory.
    for test in ("documenttest.cpp", "editformstest.cpp"):
        text = (source / "autotests" / test).read_text(encoding="utf-8")
        if "QStandardPaths::setTestModeEnabled(true);" not in text:
            raise ValueError(f"Use a source revision with Qt test-profile isolation in {test}")
    if any((prefix / name).exists() for name in KEY_FILES):
        raise ValueError("Restore only dependency files before building Okular")
    if shutil.disk_usage(build_dir.parent).free < 4 * GIB:
        raise ValueError("Need at least 4 GiB free for the Okular build, install and tests")
    make = prefix / "dev-utils/bin/jom.exe"
    build_dir.mkdir()
    get_environment = runpy.run_path(str(Path(__file__).with_name("windows-craft-recovery.py")))["developer_environment"]
    base = get_environment(vcvars, build_dir)
    preflight(prefix, cmake, base)
    env = {key: value for key, value in base.items() if key in {
        "SYSTEMROOT", "WINDIR", "SYSTEMDRIVE", "COMSPEC", "PATHEXT", "USERPROFILE",
        "APPDATA", "LOCALAPPDATA", "PROGRAMDATA", "PROGRAMFILES", "PROGRAMFILES(X86)",
        "INCLUDE", "LIB", "LIBPATH", "WINDOWSSDKVERSION", "VCTOOLSVERSION",
    }}
    for key in ("INCLUDE", "LIB", "LIBPATH"):
        if str(original_prefix).casefold() in env.get(key, "").casefold():
            raise ValueError(f"Developer environment references the original prefix in {key}")
    developer_paths = []
    for entry in base["PATH"].split(os.pathsep):
        if entry and not Path(entry).is_relative_to(original_prefix):
            developer_paths.append(entry)
    env["PATH"] = os.pathsep.join(map(str, (prefix / "bin", prefix / "dev-utils/bin", *developer_paths)))
    temp = build_dir / "temp"
    temp.mkdir()
    env["TEMP"] = env["TMP"] = str(temp)
    env["QT_PLUGIN_PATH"] = str(prefix / "plugins")

    # Keep Qt's Windows Craft directory layout when relocating the binary packages.
    qt_conf = prefix / "bin/qt.conf"
    if not qt_conf.exists():
        qt_conf.write_text("[Paths]\nPrefix=..\nLibraryExecutables=bin\n", encoding="ascii")
    relocated = relocate_metadata(prefix, original_prefix)
    receipt = {"source_commit": commit, "prefix": str(prefix), "relocated_metadata": relocated, "configure_env": {
        "msvc_toolset": env.get("VCTOOLSVERSION"), "windows_sdk": env.get("WINDOWSSDKVERSION"),
        "qt_plugin_path": env["QT_PLUGIN_PATH"],
    }, "steps": []}
    receipt_file = build_dir / "receipt.json"

    def run(name, command, test_env=None):
        with (build_dir / (name + ".log")).open("xb") as log:
            print(name, flush=True)
            result = subprocess.run(command, cwd=build_dir, env=test_env or env, stdout=log,
                                    stderr=subprocess.STDOUT)
        receipt["steps"].append(dict(name=name, command=list(map(str, command)), returncode=result.returncode))
        receipt_file.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        result.check_returncode()

    run("qtpaths", [prefix / "bin/qtpaths.exe", "--query"])
    qt_paths = (build_dir / "qtpaths.log").read_text(encoding="utf-8").replace("\\", "/").casefold()
    if f"qt_install_prefix:{prefix.as_posix().casefold()}\n" not in qt_paths:
        raise ValueError("Qt is not resolving its own paths to the restored prefix")
    run("configure", [
        cmake, "-S", source, "-B", build_dir, "-G", "NMake Makefiles",
        f"-DCMAKE_MAKE_PROGRAM={make.as_posix()}", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}",
        f"-DCMAKE_IGNORE_PREFIX_PATH={original_prefix.as_posix()}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
        "-DBUILD_TESTING=ON", "-DBUILD_SHARED_LIBS=ON", "-DBUILD_WITH_QT6=ON",
    ])
    old_root = original_prefix.as_posix().casefold()
    for metadata in (build_dir / "CMakeCache.txt", *build_dir.rglob("flags.make"), *build_dir.rglob("link.txt")):
        for line in metadata.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("CMAKE_IGNORE_PREFIX_PATH:") and old_root in line.replace("\\", "/").casefold():
                raise ValueError(f"Build metadata refers to the original prefix: {metadata}")
    run("compile", [cmake, "--build", build_dir])
    run("install", [cmake, "--install", build_dir])
    test_env = dict(env, QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1")
    receipt["loaded_modules"] = smoke(prefix, original_prefix, source / "autotests/data/simple-multipage.pdf",
                                      build_dir / "installed-pdf.log", test_env)
    receipt_file.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    run("ctest", [cmake.with_name("ctest.exe"), "--test-dir", build_dir, "--output-on-failure"], test_env)

    cases = [
        "testTabPaletteTracksColorScheme", "testTabWidthsStayStableAcrossSelection",
        "testTabControlsRemainAvailable", "testOverflowArrowsSelectEveryTab",
        "testRtlOverflowControlsFollowVisualOrder", "testCenterButtonFramesActiveTab",
        "testPageNumberControlWidthStaysStable", "testToolbarPageCounterStaysPutAcrossTabs",
        "testDuplicateOpenShowsPageNotice", "testRecentHistoryHasUniqueNewestEntries", "testOpenDocumentSessionRestoresTabs",
        "testTemporarilyMissingTabSurvivesRestore",
    ]
    run("shell", [build_dir / "bin/mainshelltest.exe", *cases,
                  "-o", str(build_dir / "shell-results.txt") + ",txt"], test_env)
    installed = {}
    for path in KEY_FILES:
        installed[path] = identity(prefix / path)
    receipt["installed"] = installed
    receipt_file.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    export = commands.add_parser("capture")
    export.add_argument("--craft-root", type=Path, required=True)
    export.add_argument("--install-manifest", type=Path, required=True)
    export.add_argument("--output", type=Path, required=True)
    restore = commands.add_parser("restore")
    restore.add_argument("--bundle", type=Path, required=True)
    restore.add_argument("--destination", type=Path, required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("--bundle", type=Path, required=True)
    verify.add_argument("--kind", choices=("dependencies", "installed-okular"), required=True)
    check = commands.add_parser("preflight")
    for option in ("prefix", "cmake", "vcvars"):
        check.add_argument(f"--{option}", type=Path, required=True)
    preview = commands.add_parser("launch")
    preview.add_argument("--prefix", type=Path, required=True)
    preview.add_argument("documents", type=Path, nargs="*")
    compile_app = commands.add_parser("build")
    for option in ("source", "prefix", "build-dir", "cmake", "vcvars", "original-prefix"):
        compile_app.add_argument(f"--{option}", type=Path, required=True)
    compile_app.add_argument("--commit", required=True)
    args = parser.parse_args()
    if args.action == "capture":
        capture(args.craft_root.resolve(), args.install_manifest, args.output.resolve())
    elif args.action == "restore":
        verify_or_restore(args.bundle, "dependencies", args.destination.resolve())
    elif args.action == "verify":
        verify_or_restore(args.bundle, args.kind, None)
    elif args.action == "preflight":
        get_environment = runpy.run_path(str(Path(__file__).with_name("windows-craft-recovery.py")))["developer_environment"]
        with tempfile.TemporaryDirectory() as temporary:
            base = get_environment(args.vcvars.resolve(), Path(temporary))
            preflight(args.prefix.resolve(), args.cmake.resolve(), base)
    elif args.action == "launch":
        launch_preview(args.prefix.resolve(), [document.resolve() for document in args.documents])
    else:
        # Inherit suppression of native missing-DLL and crash dialog boxes.
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
        build(args.source.resolve(), args.prefix.resolve(), args.build_dir.resolve(), args.cmake.resolve(),
              args.vcvars.resolve(), args.original_prefix.resolve(), args.commit)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, sqlite3.Error, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
