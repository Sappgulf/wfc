import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "wfc"


class QualityStudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make"], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def run_wfc(self, *args, env=None):
        merged = os.environ.copy()
        if env:
            merged.update(env)
        return subprocess.run([os.fspath(BINARY), *args], cwd=ROOT,
                              env=merged, capture_output=True, text=True,
                              timeout=10)

    def test_delta_is_registered_as_a_new_world(self):
        result = self.run_wfc("--list-modes")
        self.assertEqual(result.returncode, 0, result.stderr)
        modes = result.stdout.splitlines()
        self.assertEqual(len(modes), 25)
        self.assertIn("delta", modes)

    def test_report_contains_reproducibility_quality_thermo_and_studio(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report = Path(temp_dir) / "world.json"
            result = self.run_wfc(
                "--mode", "delta", "--seed", "7", "--w", "8", "--h", "6",
                "--once", "--report", os.fspath(report),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema"], 2)
            self.assertEqual(payload["mode"], "delta")
            self.assertEqual(payload["seed"], 7)
            self.assertEqual(payload["dimensions"], {"w": 8, "h": 6})
            self.assertEqual(payload["solver"], "classic")
            self.assertEqual(payload["quality"]["focus"], "delta")
            self.assertEqual(set(payload["quality"]["profile_weights"]), {
                "validity", "boundary", "coverage", "diversity", "smoothness",
                "stability", "topology",
            })
            hotspot = payload["quality"]["hotspot"]
            self.assertIn(hotspot["reason"], {"entropy", "boundary", "coverage",
                                                "branch", "validity", "balanced"})
            self.assertGreaterEqual(hotspot["score"], 0.0)
            self.assertLessEqual(hotspot["score"], 1.0)
            for key in ("total", "validity", "boundary", "coverage", "diversity",
                        "smoothness", "stability", "topology"):
                self.assertIn(key, payload["quality"])
                self.assertGreaterEqual(payload["quality"][key], 0.0)
                self.assertLessEqual(payload["quality"][key], 1.0)
            for key in ("enabled", "sampler", "observations", "proposals",
                        "accepted", "rejected", "contradictions"):
                self.assertIn(key, payload["thermo"])
            self.assertEqual(payload["studio"]["pins"], 0)
            self.assertEqual(payload["macro"]["name"], "delta-channel")
            self.assertGreater(payload["macro"]["guided_cells"], 0)
            self.assertEqual(payload["evolution"]["candidates"], 0)

    def test_fullscreen_flag_keeps_headless_dimensions_deterministic(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report = Path(temp_dir) / "headless-fullscreen.json"
            result = self.run_wfc(
                "--fullscreen", "--mode", "delta", "--seed", "7",
                "--w", "8", "--h", "6", "--once", "--report",
                os.fspath(report),
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(payload["dimensions"], {"w": 8, "h": 6})

    def test_evolution_lab_is_reproducible_and_reports_a_winner(self):
        args = ("--mode", "delta", "--seed", "7", "--w", "8", "--h", "6",
                "--once", "--evolve", "3")
        first = self.run_wfc(*args)
        second = self.run_wfc(*args)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertIn("evolution candidates=3", first.stdout)
        self.assertRegex(first.stdout, r"winner_seed=\d+ quality=0\.\d+")

    def test_debug_quality_identifies_delta_profile(self):
        result = self.run_wfc(
            "--mode", "delta", "--seed", "7", "--w", "8", "--h", "6", "--once",
            env={"WFC_DEBUG": "1"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("quality=", result.stderr)
        self.assertIn("focus=delta", result.stderr)

    def test_help_surfaces_observatory_studio_and_report_controls(self):
        result = self.run_wfc("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--report FILE", result.stdout)
        self.assertIn("l observatory", result.stdout)
        self.assertIn("P pin", result.stdout)
        self.assertIn("Q heatmap", result.stdout)
        self.assertIn("E evolution", result.stdout)
        self.assertIn("--evolve N", result.stdout)
        self.assertIn("--fullscreen", result.stdout)

    def test_help_lists_every_registered_mode(self):
        """--help renders the mode list from MODES[], so it cannot go stale."""
        modes = self.run_wfc("--list-modes").stdout.split()
        self.assertGreater(len(modes), 20)
        help_text = self.run_wfc("--help").stdout
        self.assertIn("one of %d worlds" % len(modes), help_text)
        for mode in modes:
            self.assertIn(mode, help_text)


if __name__ == "__main__":
    unittest.main()
