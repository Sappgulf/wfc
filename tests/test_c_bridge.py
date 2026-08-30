import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "wfc"
FAKE_WORKER = ROOT / "tests" / "fake_thermo.py"
REAL_WORKER = ROOT / "wfc_thermo.py"


class CBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make"], cwd=ROOT, check=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True)

    def run_wfc(self, worker, *extra, profile_dir=None):
        env = os.environ.copy()
        env["WFC_THERMO_PY"] = os.fspath(worker)
        args = [os.fspath(BINARY), "--mode", "circuit", "--solver", "thermo",
                "--once", "--w", "6", "--h", "4", "--seed", "7"]
        if profile_dir is not None:
            args.extend(["--thermo-profile", os.fspath(profile_dir)])
        args.extend(extra)
        return subprocess.run(args, cwd=ROOT, env=env, capture_output=True,
                              text=True, timeout=10)

    def test_fake_worker_uses_incremental_bridge(self):
        result = self.run_wfc(FAKE_WORKER)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("OK mode=circuit 6x4", result.stdout)
        self.assertIn("tries=1 steps=0", result.stdout)
        self.assertNotIn("thermo failed", result.stderr)

    def test_c_rejects_worker_completion_that_breaks_hard_edges(self):
        env = os.environ.copy()
        env["WFC_THERMO_PY"] = os.fspath(FAKE_WORKER)
        env["FAKE_INVALID_DONE"] = "1"
        result = subprocess.run(
            [os.fspath(BINARY), "--mode", "circuit", "--solver", "thermo",
             "--once", "--w", "6", "--h", "4", "--seed", "7"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("OK mode=circuit 6x4", result.stdout)
        self.assertIn("tries=2 steps=24", result.stdout)

    def test_real_worker_persists_and_no_learn_is_ephemeral(self):
        with tempfile.TemporaryDirectory() as profile_dir:
            result = self.run_wfc(REAL_WORKER, profile_dir=profile_dir)
            self.assertEqual(result.returncode, 0, result.stderr)
            profiles = list(Path(profile_dir).glob("*.json"))
            self.assertEqual(len(profiles), 1)
            learned = profiles[0].read_text(encoding="utf-8")
            self.assertIn('"observations":', learned)

            no_learn = self.run_wfc(REAL_WORKER, "--no-learn",
                                    profile_dir=profile_dir)
            self.assertEqual(no_learn.returncode, 0, no_learn.stderr)
            self.assertEqual(len(list(Path(profile_dir).glob("*.json"))), 1)


if __name__ == "__main__":
    unittest.main()
