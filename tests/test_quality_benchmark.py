import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tests" / "quality_benchmark.py"


class QualityBenchmarkTests(unittest.TestCase):
    def test_benchmark_covers_solver_paths_and_network_modes(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--binary", str(ROOT / "wfc"),
             "--trials", "1", "--w", "8", "--h", "6"],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("QUALITY BENCHMARK", result.stdout)
        payload = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(payload["schema"], 1)
        self.assertEqual(set(payload["solvers"]), {
            "classic", "thermo-ephemeral", "thermo-learned",
        })
        self.assertEqual({item["mode"] for item in payload["results"]},
                         {"streets", "neurons", "mycelium", "delta"})
        self.assertTrue(all(item["success"] for item in payload["results"]))


if __name__ == "__main__":
    unittest.main()
