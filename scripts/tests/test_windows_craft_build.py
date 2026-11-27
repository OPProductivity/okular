# SPDX-FileCopyrightText: None
# SPDX-License-Identifier: CC0-1.0

import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import Mock


SCRIPTS = Path(__file__).resolve().parents[1]
BUILD = runpy.run_path(str(SCRIPTS / "windows-craft-build.py"))
STORAGE = runpy.run_path(str(SCRIPTS / "windows-craft-storage.py"))


class PublicBuildTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.work = Path(temp.name).resolve()

    def test_passed_checkpoint_requires_matching_inputs_and_outputs(self):
        artifact = self.work / "output"
        inputs = {"input": "first"}
        action = Mock(side_effect=lambda: artifact.write_text("result", encoding="utf-8"))
        BUILD["Stages"](self.work).run("test", inputs, [artifact], action)
        BUILD["Stages"](self.work).run("test", inputs, [artifact], action)
        action.assert_called_once()
        artifact.write_text("modified", encoding="utf-8")
        BUILD["Stages"](self.work).run("test", inputs, [artifact], action)
        BUILD["Stages"](self.work).run("test", {"input": "second"}, [artifact], action)
        artifact.unlink()
        record = BUILD["Stages"](self.work).run("test", {"input": "second"}, [artifact], action)
        self.assertEqual(action.call_count, 4)
        self.assertEqual(len(record["history"]), 3)

    def test_failed_and_interrupted_attempts_are_retried(self):
        artifact = self.work / "output"
        runner = BUILD["Stages"](self.work)
        failure = Mock(side_effect=KeyboardInterrupt)
        with self.assertRaises(KeyboardInterrupt):
            runner.run("test", {}, [artifact], failure)
        journal = self.work / "build-stages.json"
        saved = json.loads(journal.read_text(encoding="utf-8"))
        self.assertEqual(saved["test"]["status"], "failed")
        saved["test"]["status"] = "running"
        BUILD["write_json"](journal, saved)
        artifact.write_text("result", encoding="utf-8")
        retry = Mock(return_value=None)
        record = BUILD["Stages"](self.work).run("test", {}, [artifact], retry)
        retry.assert_called_once()
        self.assertEqual(record["attempt"], 2)
        self.assertEqual(record["status"], "passed")

    def test_workspace_requires_existing_marker_for_storage(self):
        (self.work / "unrelated.txt").touch()
        with self.assertRaisesRegex(ValueError, "public build workspace"):
            BUILD["storage"](self.work, True)
        self.assertTrue((self.work / "unrelated.txt").is_file())

    def test_recycle_requires_verification_from_this_workspace(self):
        BUILD["owned_workspace"](self.work, create=True)
        generated = self.work / "build"
        generated.mkdir()
        (generated / "generated.txt").touch()
        with self.assertRaisesRegex(ValueError, "PDF startup verification"):
            BUILD["storage"](self.work, True)
        self.assertTrue((generated / "generated.txt").is_file())

    def test_poppler_setting_preserves_other_blueprint_options(self):
        etc = self.work / "etc"
        etc.mkdir()
        options = etc / "BlueprintSettings.ini"
        options.write_text("[qt-libs/poppler]\nbuildTests = True\nbuildCppFrontend = True\n"
                           "[unrelated]\ncustom = Keep Case\n", encoding="utf-8")
        BUILD["set_blueprint_options"](self.work)
        updated = options.read_text(encoding="utf-8")
        self.assertIn("buildTests = False", updated)
        self.assertIn("buildCppFrontend = True", updated)
        self.assertIn("custom = Keep Case", updated)
        self.assertIn("ignored = True", updated)
        BUILD["set_blueprint_options"](self.work)
        self.assertEqual(updated, options.read_text(encoding="utf-8"))

    def test_package_route_requires_positive_evidence(self):
        log = self.work / "craft.log"
        log.write_text(
            "*** Handling package: libs/sourced, action: all ***\n"
            "*** Action: fetch-binary for libs/sourced ***\n"
            "libs/sourced not found in cache\n"
            "*** Action: post-qmerge for libs/sourced ***\n"
            "*** Handling package: libs/cached, action: all ***\n"
            "*** Action: fetch-binary for libs/cached ***\n"
            "Extracting archive: cache.7z\n"
            "*** Handling package: dev-utils/bootstrap, action: all ***\n"
            "*** Action: fetch-binary for dev-utils/bootstrap ***\n"
            "curl https://example.invalid/craft/bootstrap.tar.xz\n"
            "*** Craft all succeeded: dev-utils/bootstrap after 1s ***\n"
            "*** Handling package: libs/failed, action: all ***\n"
            "*** Action: fetch-binary for libs/failed ***\n"
            "Extracting archive: cache2.7z\n"
            "libs/failed not found in cache\n"
            "fatal error: package libs/failed all failed\n", encoding="utf-8")
        routes = BUILD["package_routes"]([log])
        self.assertEqual(routes["libs/sourced"]["route"], "Craft recipe")
        self.assertEqual(routes["libs/cached"]["route"], "Craft binary cache")
        self.assertEqual(routes["dev-utils/bootstrap"]["route"], "Craft binary cache")
        self.assertEqual(routes["dev-utils/bootstrap"]["archive"], "bootstrap.tar.xz")
        self.assertNotIn("libs/failed", routes)

    def test_storage_targets_only_allow_generated_directories(self):
        for name in ("build", "prefix/build", "prefix/download", "prefix/bin"):
            path = self.work / name
            path.mkdir(parents=True)
            (path / "example.txt").write_bytes(b"a" * 128)
        self.assertEqual(list(STORAGE["targets"](self.work)),
                         [self.work / "build", self.work / "prefix/build"])
        self.assertEqual(STORAGE["measure"](self.work / "prefix", exclude=(self.work / "prefix/build",)), 256)


if __name__ == "__main__":
    unittest.main()
