import json
import subprocess
import sys
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tests" / "quality_benchmark.py"


class QualityBenchmarkTests(unittest.TestCase):
    def test_benchmark_covers_solver_paths_and_network_modes(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifact = Path(tmp) / "benchmark.json"
            result = subprocess.run(
                [sys.executable, str(SCRIPT), "--binary", str(ROOT / "wfc"),
                 "--trials", "1", "--w", "8", "--h", "6", "--json-out",
                 str(artifact)], cwd=ROOT, capture_output=True, text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(artifact.exists())
            self.assertIn("QUALITY BENCHMARK", result.stdout)
            payload = json.loads(artifact.read_text(encoding="utf-8"))
        self.assertEqual(payload["schema"], 2)
        self.assertEqual(set(payload["solvers"]), {
            "classic", "thermo-ephemeral", "thermo-learned",
        })
        self.assertEqual({item["mode"] for item in payload["results"]},
                         {"streets", "neurons", "mycelium", "delta", "rail"})
        self.assertTrue(all(item["success"] for item in payload["results"]))
        self.assertEqual(set(payload["summary"]), {
            "classic", "thermo-ephemeral", "thermo-learned",
        })
        for solver in payload["summary"]:
            self.assertIn("median_ms", payload["summary"][solver])
            self.assertIn("p95_ms", payload["summary"][solver])

    def test_profile_isolation_for_non_classic_solvers(self):
        run_calls = []
        with tempfile.TemporaryDirectory() as tmp:
            profile_root = Path(tmp)

            def fake_run(cmd, *cmd_args, **kwargs):
                report_idx = cmd.index("--report")
                report = Path(cmd[report_idx + 1])
                report.parent.mkdir(parents=True, exist_ok=True)
                report.write_text(json.dumps({
                    "quality": {"total": 0.5},
                }), encoding="utf-8")
                run_calls.append(cmd)
                return subprocess.CompletedProcess(cmd, 0, "", "")

            with patch("tests.quality_benchmark.subprocess.run", side_effect=fake_run):
                import tests.quality_benchmark as qb
                for solver in ("classic", "thermo-ephemeral", "thermo-learned"):
                    qb.run_case(str(profile_root / "wfc"), "streets", solver, 0, 8, 6,
                                profile_root / "profiles")

            self.assertEqual(run_calls[0][run_calls[0].index("--solver") + 1], "classic")
            self.assertNotIn("--thermo-profile", run_calls[0])
            self.assertEqual(run_calls[1][run_calls[1].index("--thermo-profile") + 1],
                             str(profile_root / "profiles" / "streets" / "thermo-ephemeral" / "trial-0"))
            self.assertEqual(run_calls[2][run_calls[2].index("--thermo-profile") + 1],
                             str(profile_root / "profiles" / "streets" / "thermo-learned" / "trial-0"))


if __name__ == "__main__":
    unittest.main()
