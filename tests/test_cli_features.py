import json
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


import tests.sandbox as sandbox


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "wfc"
WORLD_HASH_OFFSET = 1469598103934665603
WORLD_HASH_PRIME = 1099511628211


def write_world_fixture(path):
    body = struct.pack(
        "<IIIIQII", 0, 2, 2, 14, 0x0102030405060708, 500, 4,
    )
    body += struct.pack("<QQQQ", 1, 2, 4, 8)
    body += struct.pack("<I", 1)
    body += struct.pack("<II", 2, 2)
    checksum = WORLD_HASH_OFFSET
    for byte in body:
        checksum = ((checksum ^ byte) * WORLD_HASH_PRIME) & ((1 << 64) - 1)
    path.write_bytes(
        struct.pack("<II", 0x31434657, 1) + body + struct.pack("<Q", checksum)
    )


class CliFeatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "wfc"], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def run_wfc(self, *args, timeout=30):
        return subprocess.run([os.fspath(BINARY), *args], cwd=ROOT,
                              env=sandbox.env(), capture_output=True, text=True,
                              timeout=timeout)

    def test_export_surfaces_write_their_declared_formats(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            outputs = {
                "png": root / "map.png",
                "bmp": root / "map.bmp",
                "gif": root / "map.gif",
                "gallery": root / "gallery.html",
                "collage": root / "collage.png",
            }
            commands = (
                ("--mode", "fire", "--seed", "9", "--w", "8", "--h", "6",
                 "--once", "--save", os.fspath(outputs["png"])),
                ("--mode", "fire", "--seed", "9", "--w", "8", "--h", "6",
                 "--once", "--save", os.fspath(outputs["bmp"])),
                ("--mode", "fire", "--seed", "9", "--w", "8", "--h", "6",
                 "--once", "--gif", os.fspath(outputs["gif"])),
                ("--gallery", os.fspath(outputs["gallery"])),
                ("--collage", os.fspath(outputs["collage"])),
            )
            for command in commands:
                result = self.run_wfc(*command, timeout=45)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(outputs["png"].read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertTrue(outputs["bmp"].read_bytes().startswith(b"BM"))
            self.assertTrue(outputs["gif"].read_bytes().startswith(b"GIF89a"))
            self.assertIn("circuit", outputs["gallery"].read_text(encoding="utf-8"))
            self.assertTrue(outputs["collage"].read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

    def test_help_names_the_world_inspector(self):
        result = self.run_wfc("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--inspect-world FILE", result.stdout)

    def test_inspect_world_reports_metadata_and_rejects_tampering(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            valid = root / "valid.wfc"
            write_world_fixture(valid)
            result = self.run_wfc("--inspect-world", os.fspath(valid))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), {
                "format": "WFC1",
                "version": 1,
                "mode": "circuit",
                "dimensions": {"w": 2, "h": 2},
                "tiles": 14,
                "seed": 0x0102030405060708,
                "bias": 0.5,
                "pins": 1,
                "decided": 4,
            })

            truncated = root / "truncated.wfc"
            truncated.write_bytes(valid.read_bytes()[:-1])
            result = self.run_wfc("--inspect-world", os.fspath(truncated))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid world snapshot", result.stderr)

            tampered = bytearray(valid.read_bytes())
            tampered[24] ^= 1
            tampered_path = root / "tampered.wfc"
            tampered_path.write_bytes(tampered)
            result = self.run_wfc("--inspect-world", os.fspath(tampered_path))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid world snapshot", result.stderr)


if __name__ == "__main__":
    unittest.main()
