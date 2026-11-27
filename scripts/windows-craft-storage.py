#!/usr/bin/env python3
# SPDX-FileCopyrightText: None
# SPDX-License-Identifier: CC0-1.0
"""Disk accounting and recoverable cleanup for the public Windows Craft build."""

import csv
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import stat
import subprocess
from uuid import UUID


GENERATED = ("build", "prefix/build")
GIB = 1024 ** 3


def reparse_point(info):
    return bool(getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def measure(root, *, exclude=(), refuse_links=False):
    if not os.path.lexists(root):
        return 0
    total = 0
    excluded = {os.fspath(path) for path in exclude}
    pending = [(os.fspath(root), root.lstat())]
    while pending:
        path, info = pending.pop()
        if reparse_point(info) or stat.S_ISLNK(info.st_mode):
            if refuse_links:
                raise ValueError(f"Recycling requires an ordinary tree without reparse points: {path}")
        elif stat.S_ISDIR(info.st_mode):
            with os.scandir(path) as entries:
                pending.extend((entry.path, entry.stat(follow_symlinks=False))
                               for entry in entries if entry.path not in excluded)
        elif stat.S_ISREG(info.st_mode):
            total += info.st_size
    return total


def disk_report(work):
    prefix = work / "prefix"
    downloads = prefix / "download"
    dependencies = prefix / "build"
    app = work / "build"
    bootstrap = work / "bootstrap-python"
    evidence = sum(measure(path) for path in work.iterdir() if path.is_file() and path.name != ".public-build.lock")
    other = sum(measure(path) for path in work.iterdir()
                if path not in (prefix, app, bootstrap) and not path.is_file())
    groups = (
        ("Downloads and package archives", measure(downloads)),
        ("Craft dependency work and package images", measure(dependencies)),
        ("Okular app build and test binaries", measure(app)),
        ("Craft prefix, installed runtime and tools", measure(prefix, exclude=(downloads, dependencies))),
        ("Bootstrap Python environment", measure(bootstrap)),
        ("Workspace logs, stage records and other root files", evidence),
        ("Other workspace directories", other),
    )
    for name, size in groups:
        print(f"{name:51} {size / GIB:7.2f} GiB ({size:,} bytes)", flush=True)
    print("Logical file sizes. Hard-linked files can be counted more than once. "
          "Junctions and other reparse points are excluded.")


def targets(work):
    # Require an unchanged canonical path through every component, including
    # the parent prefix. Do not follow junctions to a different tree.
    for name in GENERATED:
        path = work / name
        if not os.path.lexists(path):
            continue
        for component in (path, *path.parents):
            if reparse_point(component.lstat()):
                raise ValueError(f"Refusing to recycle through the reparse point {component}")
            if component == work:
                break
        if path.resolve(strict=True) != path or not path.is_dir():
            raise ValueError(f"Expected an ordinary directory at {path}")
        measure(path, refuse_links=True)
        yield path


def recycle_records(folder):
    return set(folder.glob("$I*"))


def original_path(metadata):
    data = metadata.read_bytes()
    version = int.from_bytes(data[:8], "little")
    if version in (1, 2):
        start = 24 if version == 1 else 28
        return Path(data[start:].decode("utf-16-le").split("\0", 1)[0])
    return None


def shell_recycle(path):
    """Request the Windows recycle-only operation via IFileOperation."""
    pointer = ctypes.c_void_p
    hresult = ctypes.c_long
    ole = ctypes.WinDLL("ole32")
    shell = ctypes.WinDLL("shell32")

    def guid(value):
        return (ctypes.c_ubyte * 16).from_buffer_copy(UUID(value).bytes_le)

    def check(code):
        if code < 0:
            raise OSError(f"Windows Recycle Bin operation failed (HRESULT 0x{code & 0xffffffff:08x}).")

    def method(interface, index, result, *arguments):
        table = ctypes.cast(interface, ctypes.POINTER(ctypes.POINTER(pointer)))[0]
        function = ctypes.WINFUNCTYPE(result, pointer, *arguments)(table[index])
        return lambda *args: function(interface, *args)

    ole.CoInitializeEx.argtypes = (pointer, wintypes.DWORD)
    ole.CoInitializeEx.restype = hresult
    ole.CoUninitialize.argtypes = ()
    ole.CoUninitialize.restype = None
    ole.CoCreateInstance.argtypes = (pointer, pointer, wintypes.DWORD, pointer, ctypes.POINTER(pointer))
    ole.CoCreateInstance.restype = hresult
    shell.SHCreateItemFromParsingName.argtypes = (wintypes.LPCWSTR, pointer, pointer, ctypes.POINTER(pointer))
    shell.SHCreateItemFromParsingName.restype = hresult

    check(ole.CoInitializeEx(None, 2))  # COINIT_APARTMENTTHREADED
    operation = pointer()
    item = pointer()
    try:
        clsid = guid("3ad05575-8857-4850-9277-11b85bdb8e09")
        file_operation = guid("947aab5f-0a5c-4c13-b4d6-4bf7836fc9f8")
        shell_item = guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")
        check(ole.CoCreateInstance(clsid, None, 1, file_operation, ctypes.byref(operation)))
        check(shell.SHCreateItemFromParsingName(str(path), None, shell_item, ctypes.byref(item)))
        # FOFX_RECYCLEONDELETE, FOFX_EARLYFAILURE, FOF_NOERRORUI, FOF_SILENT, FOF_WANTNUKEWARNING.
        flags = 0x80000 | 0x100000 | 0x400 | 0x4 | 0x4000
        check(method(operation, 5, hresult, wintypes.DWORD)(flags))  # SetOperationFlags
        check(method(operation, 18, hresult, pointer, pointer)(item, None))  # DeleteItem
        check(method(operation, 21, hresult)())  # PerformOperations
        aborted = wintypes.BOOL()
        check(method(operation, 22, hresult, ctypes.POINTER(wintypes.BOOL))(ctypes.byref(aborted)))
        if aborted.value:
            raise ValueError(f"Windows canceled the Recycle Bin operation for {path}.")
    finally:
        for interface in (item, operation):
            if interface:
                method(interface, 2, wintypes.ULONG)()  # IUnknown::Release
        ole.CoUninitialize()


def recycle(work):
    paths = list(targets(work))
    if not paths:
        print("There are no generated build directories to recycle.")
        return []
    drive_type = ctypes.WinDLL("kernel32").GetDriveTypeW
    drive_type.argtypes = (wintypes.LPCWSTR,)
    if drive_type(work.anchor) != 3:  # DRIVE_FIXED
        raise ValueError("Recycle Bin cleanup is available only on a local fixed drive.")
    whoami = Path(os.environ["SYSTEMROOT"]) / "System32/whoami.exe"
    result = subprocess.check_output([whoami, "/user", "/fo", "csv", "/nh"], text=True)
    _, sid = next(csv.reader(result.splitlines()))
    bin_dir = Path(work.anchor) / "$Recycle.Bin" / sid
    moved = []
    for path in paths:
        before = recycle_records(bin_dir)
        shell_recycle(path)
        matches = [
            info for info in recycle_records(bin_dir) - before
            if original_path(info) == path and info.with_name("$R" + info.name[2:]).is_dir()
        ]
        if os.path.lexists(path) or len(matches) != 1:
            raise ValueError(f"Could not verify a restorable Recycle Bin entry for {path}.")
        moved.append(str(path))
        print(f"Verified in the Recycle Bin: {path}", flush=True)
    return moved
