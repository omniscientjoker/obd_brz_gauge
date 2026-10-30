#!/usr/bin/env python3
"""Host checks for target isolation and transactional release packaging."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from gen_release import BINS, generate_release


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.master = self.make_build("master")
        self.slave = self.make_build("slave")
        self.output = self.root / "release"
        self.output.mkdir()
        (self.output / "old.bin").write_bytes(b"previous release")

    def make_build(self, target):
        path = self.root / f"build-{target}"
        (path / "config").mkdir(parents=True)
        (path / "config/sdkconfig.h").write_text(
            f"#define CONFIG_GAUGE_FIRMWARE_{target.upper()} 1\n")
        (path / "project_description.json").write_text(json.dumps({
            "project_name": "obd_brz_gauge", "project_version": "test-dirty"}))
        (path / "build.ninja").write_text(
            r'-DOBD_GAUGE_BUILD_TAG=\"branch-42-abc123\" '
            r'-DOBD_GAUGE_GIT_HASH=\"abc123\" -DOBD_GAUGE_GIT_BRANCH=\"branch\" '
            r'-DOBD_GAUGE_GIT_COUNT=42 -DIDF_VER=\"v5.5.3\"')
        for key, relative in BINS.items():
            artifact = path / relative
            artifact.parent.mkdir(parents=True, exist_ok=True)
            artifact.write_bytes(f"{target}:{key}".encode())
        (path / "flasher_args.json").write_text(json.dumps({
            "write_flash_args": ["--flash_mode", "dio", "--flash_size", "16MB"],
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0xf000": "ota_data_initial.bin",
                "0x20000": "obd_brz_gauge.bin",
                "0xa20000": "bootmedia.bin",
            },
        }))
        return path

    def test_package_hashes_flash_paths_and_master_ota_compatibility(self):
        generate_release(self.master, self.slave, self.output)
        self.assertFalse((self.output / "old.bin").exists())
        for location in (self.output, self.output / "master", self.output / "slave"):
            manifest = json.loads((location / "latest.json").read_text())
            for info in manifest["files"].values():
                data = (location / info["path"]).read_bytes()
                self.assertEqual(len(data), info["size"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), info["sha256"])
        for target in ("master", "slave"):
            args = (self.output / target / "flash_args").read_text()
            self.assertIn(f"0x20000\nobd_brz_gauge_{target}.bin", args)
            self.assertIn("0xa20000\nbootmedia.bin", args)
        root = json.loads((self.output / "latest.json").read_text())
        self.assertEqual(root["firmware"]["target"], "master")
        self.assertEqual(root["firmware"]["count"], 42)
        self.assertEqual(root["files"]["firmware"]["path"], "master/obd_brz_gauge_master.bin")

    def test_swapped_targets_preserve_previous_release(self):
        with self.assertRaisesRegex(ValueError, "target is not master"):
            generate_release(self.slave, self.master, self.output)
        self.assertEqual((self.output / "old.bin").read_bytes(), b"previous release")

    def test_missing_artifact_preserves_previous_release(self):
        (self.slave / "bootmedia.bin").unlink()
        with self.assertRaisesRegex(ValueError, "missing build artifact"):
            generate_release(self.master, self.slave, self.output)
        self.assertTrue((self.output / "old.bin").is_file())

    def test_mismatched_builds_preserve_previous_release(self):
        ninja = self.slave / "build.ninja"
        ninja.write_text(ninja.read_text().replace("abc123", "def456"))
        with self.assertRaisesRegex(ValueError, "metadata differs"):
            generate_release(self.master, self.slave, self.output)
        self.assertTrue((self.output / "old.bin").is_file())


if __name__ == "__main__":
    unittest.main()
