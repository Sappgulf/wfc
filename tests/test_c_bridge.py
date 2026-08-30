import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parent))
import sandbox  # noqa: E402

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
        env = sandbox.env(WFC_THERMO_PY=os.fspath(worker))
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
        env = sandbox.env(WFC_THERMO_PY=os.fspath(FAKE_WORKER),
                          FAKE_INVALID_DONE="1")
        result = subprocess.run(
            [os.fspath(BINARY), "--mode", "circuit", "--solver", "thermo",
             "--once", "--w", "6", "--h", "4", "--seed", "7"],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("OK mode=circuit 6x4", result.stdout)
        self.assertIn("tries=2 steps=24", result.stdout)

    def test_quality_guard_displaces_weak_proposals_reproducibly(self):
        """A patch that loses to the classic pick is rolled back and counted.

        The guard runs an extra weighted_pick_at draw per round; it must do so
        on a derived rng stream, so two identical runs stay byte-identical.
        """
        reports = []
        with tempfile.TemporaryDirectory() as tmp:
            for run in range(2):
                report = Path(tmp) / ("report-%d.json" % run)
                result = self.run_wfc(FAKE_WORKER, "--report", os.fspath(report))
                self.assertEqual(result.returncode, 0, result.stderr)
                reports.append(json.loads(report.read_text(encoding="utf-8")))

        thermo = reports[0]["thermo"]
        self.assertGreater(thermo["proposals"], 0)
        self.assertGreater(thermo["displaced"], 0,
                           "guard never rejected a losing proposal")
        self.assertLessEqual(thermo["displaced"], thermo["rejected"])
        self.assertEqual(thermo["accepted"] + thermo["rejected"],
                         thermo["proposals"])
        self.assertEqual(reports[0]["quality"], reports[1]["quality"])
        self.assertEqual(reports[0]["thermo"], reports[1]["thermo"])

    def test_feedback_marks_feature_presence_and_carries_the_guard_margin(self):
        """Events say which features were used; the reward says if that was good.

        Signing the events by acceptance as well made the two negatives cancel,
        so a displaced proposal raised the bias of the tile that had just lost.
        Every bias then drifted up together, and a uniform shift is invisible
        to the worker's softmax — which is why learning did nothing at all.
        """
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "feedback.jsonl"
            env = sandbox.env(WFC_THERMO_PY=os.fspath(FAKE_WORKER),
                              FAKE_FEEDBACK_LOG=os.fspath(log))
            result = subprocess.run(
                [os.fspath(BINARY), "--mode", "circuit", "--solver", "thermo",
                 "--once", "--w", "8", "--h", "6", "--seed", "7"],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            frames = [json.loads(line)
                      for line in log.read_text(encoding="utf-8").splitlines()]

        self.assertGreater(len(frames), 0)
        displaced = [f for f in frames if f["rejected"] and not f["contradictions"]]
        self.assertGreater(len(displaced), 0, "guard never displaced a proposal")
        for frame in frames:
            self.assertIn("margin", frame)
            for key in ("tile_events", "pair_events", "context_events"):
                for event in frame[key]:
                    self.assertEqual(event["value"], 1,
                                     "%s must mark presence, not outcome" % key)
        # a displaced round must reach the learner as a loss
        self.assertTrue(all(f["margin"] < 0 for f in displaced))
        self.assertTrue(all(f["reward"] < 0 for f in displaced))

    def test_worker_proposes_connected_patches_not_single_cells(self):
        """A round should be able to express a junction, not just one tile.

        Placing exactly one cell per round meant the sidecar could only ever
        have an opinion about a single tile, never about the corner or run
        where the structure it is learning actually lives.
        """
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "feedback.jsonl"
            env = sandbox.env(WFC_THERMO_PY=os.fspath(REAL_WORKER),
                              FAKE_FEEDBACK_LOG=os.fspath(log))
            result = subprocess.run(
                [os.fspath(BINARY), "--mode", "streets", "--solver", "thermo",
                 "--once", "--w", "16", "--h", "10", "--seed", "7",
                 "--thermo-profile", tmp],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(subprocess.run(
                [os.fspath(BINARY), "--mode", "streets", "--solver", "thermo",
                 "--once", "--w", "16", "--h", "10", "--seed", "7",
                 "--thermo-profile", tmp, "--report", os.fspath(Path(tmp) / "r.json")],
                cwd=ROOT, env=env, capture_output=True, text=True,
                timeout=30).returncode == 0 and
                (Path(tmp) / "r.json").read_text(encoding="utf-8"))

        thermo = report["thermo"]
        self.assertGreater(thermo["proposals"], thermo["round"],
                           "patches must carry more than one cell per round")
        self.assertEqual(thermo["accepted"] + thermo["rejected"],
                         thermo["proposals"])

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
