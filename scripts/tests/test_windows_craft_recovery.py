import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import os
import zipfile

spec = importlib.util.spec_from_file_location("recovery", Path(__file__).parents[1] / "windows-craft-recovery.py")
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class RecoveryTests(unittest.TestCase):
    def test_candidate_scope_never_selects_sources_images_or_app_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build/libs/qt6/qtbase/work/build"
            build.mkdir(parents=True)
            (build / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Ninja")
            for protected in ("build/libs/qt6/qtbase/work/qtbase-everywhere-src-6.11.3", "build/libs/qt6/qtbase/image-RelWithDebInfo", "build/kde/applications/okular/work/build"):
                (root / protected).mkdir(parents=True)
            self.assertEqual(recovery.candidate_paths(root), [build])

    def test_source_named_build_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build = root / "build/libs/qt6/qtbase/work/build"
            build.mkdir(parents=True)
            (build / "CMakeCache.txt").write_text("cache")
            (build / "CMakeLists.txt").write_text("actual source")
            with self.assertRaisesRegex(ValueError, "source tree"):
                recovery.candidate_paths(root)

    def test_strict_inventory_rejects_nested_reparse_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "kept").write_bytes(b"x")
            with mock.patch.object(recovery, "is_reparse", side_effect=lambda path: path.name == "kept"):
                with self.assertRaisesRegex(ValueError, "Reparse entry"):
                    list(recovery.files(root, strict=True))

    def test_capture_never_reuses_existing_evidence_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "new, absent"):
                recovery.capture(root, root, root)

    def test_installed_candidate_reference_blocks_verification(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = root / "lib/cmake/Qt6/Qt6Config.cmake"
            config.parent.mkdir(parents=True)
            target = root / "build/libs/qt6/qtbase/work/build"
            config.write_text('set(IMPORT_LOCATION "' + str(target).replace("\\", "/") + '/Qt6Core.dll")')
            refs = recovery.candidate_references(root, [target])
            self.assertEqual(len(refs), 1)
            self.assertEqual(refs[0]["line"], 1)

    def test_corrupt_recovery_artifact_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "archive.zip").write_bytes(b"changed")
            (root / "recovery.json").write_text(json.dumps({"complete": True, "craft_root": tmp,
                "protected_files": [], "git": {}, "installed_candidate_references": [],
                "archives": {"archive.zip": {"sha256": "0" * 64}}}))
            with self.assertRaisesRegex(ValueError, "artifact mismatch"):
                recovery.verify(root)

    def test_archive_path_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            snapshot = root / "snapshot"
            snapshot.mkdir()
            path = root / "external"
            path.write_text("outside evidence")
            (snapshot / "recovery.json").write_text(json.dumps({"complete": True, "craft_root": tmp,
                "protected_files": [], "git": {}, "installed_candidate_references": [],
                "archives": {"../external": {"sha256": recovery.sha(path)}}}))
            with self.assertRaisesRegex(ValueError, "artifact mismatch"):
                recovery.verify(snapshot)

    def test_protected_runtime_content_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "Qt6Core.dll"
            path.write_bytes(b"original")
            rec = recovery.record(path, root)
            path.write_bytes(b"changed")
            (root / "recovery.json").write_text(json.dumps({"complete": True, "craft_root": tmp,
                "protected_files": [rec], "git": {}, "installed_candidate_references": [], "archives": {}}))
            with self.assertRaisesRegex(ValueError, "Protected input mismatch"):
                recovery.verify(root)

    def test_incomplete_capture_cannot_produce_plan(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "recovery.json").write_text(json.dumps({"complete": False}))
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                recovery.write_plan(root, root / "plan.md")

    def test_runtime_walk_prunes_builds_and_git_checkout(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("build/large/file", "craft/file", "etc/blueprints/locations/repo/file",
                         "etc/blueprints/install.db", "etc/PristineCraftenv.json", "bin/Qt6Core.dll"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"x")
            visited = []
            original = os.walk

            def watched(*args, **kwargs):
                for current, dirs, names in original(*args, **kwargs):
                    visited.append(Path(current).relative_to(root).as_posix())
                    yield current, dirs, names

            with mock.patch.object(recovery.os, "walk", watched):
                names = {path.relative_to(root).as_posix() for path in recovery.runtime_files(root)}
            self.assertEqual(names, {"etc/blueprints/install.db", "bin/Qt6Core.dll"})
            self.assertFalse(any(name == "build" or name.startswith("build/") or
                                 name == "etc/blueprints/locations" for name in visited))

    def test_legacy_environment_cache_drift_is_explicitly_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / "etc/PristineCraftenv.json"
            path.parent.mkdir()
            path.write_text('{"session":"before"}')
            rec = recovery.record(path, root)
            path.write_text('{"session":"after"}')
            state = {"complete": True, "craft_root": str(root), "protected_files": [rec],
                     "git": {}, "installed_candidate_references": [], "archives": {}}
            (root / "recovery.json").write_text(json.dumps(state))
            self.assertEqual(recovery.verify(root)["excluded_mutable"], ["etc/PristineCraftenv.json"])

    def test_archive_member_hash_must_match_protected_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "build/libs/qt6/qtsvg/work/qtsvg-everywhere-src-6.11.3/CMakeLists.txt"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"actual source")
            rec = recovery.record(source, root)
            archive = root / "qt-reconstruction-inputs.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr(rec["path"], b"different source")
            state = {"complete": True, "craft_root": str(root), "protected_files": [rec],
                     "git": {}, "installed_candidate_references": [],
                     "archives": {archive.name: {"sha256": recovery.sha(archive)}}}
            (root / "recovery.json").write_text(json.dumps(state))
            with self.assertRaisesRegex(ValueError, "member hash mismatch"):
                recovery.verify(root)


if __name__ == "__main__":
    unittest.main()
