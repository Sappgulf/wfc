#!/usr/bin/env python3
"""Golden image and terminal-render contracts for the registered worlds."""

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests" / "visual_baseline.json"


def png_dimensions(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 24:
        raise AssertionError("export is not a PNG")
    return {"w": int.from_bytes(data[16:20], "big"),
            "h": int.from_bytes(data[20:24], "big")}


class VisualRegressionTests(unittest.TestCase):
    def test_manifest_covers_every_mode_and_is_reproducible(self):
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(manifest["seed"], 4242)
        modes = subprocess.check_output(
            [str(ROOT / "wfc"), "--list-modes"], cwd=ROOT, text=True,
        ).split()
        self.assertEqual(set(manifest["sha256"]), set(modes))
        with tempfile.TemporaryDirectory(prefix="wfc-visual-") as tmp:
            env = os.environ.copy()
            env["HOME"] = tmp
            for mode in modes:
                first = Path(tmp) / (mode + "-a.png")
                second = Path(tmp) / (mode + "-b.png")
                for destination in (first, second):
                    result = subprocess.run(
                        [str(ROOT / "wfc"), "--mode", mode, "--solver", "classic",
                         "--seed", "4242", "--w", "8", "--h", "6", "--once",
                         "--no-bloom", "--no-weather", "--save", str(destination)],
                        cwd=ROOT, env=env, capture_output=True, text=True, timeout=15,
                    )
                    self.assertEqual(result.returncode, 0, "%s: %s" % (mode, result.stderr))
                first_bytes = first.read_bytes()
                second_bytes = second.read_bytes()
                self.assertEqual(first_bytes, second_bytes, mode)
                self.assertEqual(hashlib.sha256(first_bytes).hexdigest(),
                                 manifest["sha256"][mode], mode)
                self.assertEqual(png_dimensions(first_bytes), manifest["dimensions"][mode])


if __name__ == "__main__":
    unittest.main()
