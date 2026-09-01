"""End-to-end contract for the optional THRML/JAX sampler."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import tests.sandbox as sandbox


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "wfc"
THERMO_PYTHON = ROOT / ".venv-thermo" / "bin" / "python"


class AcceleratedSolverTests(unittest.TestCase):
    def test_circuit_uses_thrml_jax_when_optional_runtime_is_installed(self):
        if not THERMO_PYTHON.exists():
            self.skipTest(".venv-thermo is not installed")
        probe = subprocess.run(
            [os.fspath(THERMO_PYTHON), "-c", "import jax, thrml"],
            cwd=ROOT, capture_output=True, text=True, timeout=15,
        )
        if probe.returncode != 0:
            self.skipTest("THRML/JAX imports are unavailable")
        with tempfile.TemporaryDirectory(prefix="wfc-accelerated-") as tmp:
            report = Path(tmp) / "report.json"
            result = subprocess.run(
                [os.fspath(BINARY), "--mode", "circuit", "--seed", "7",
                 "--w", "4", "--h", "3", "--once", "--no-learn",
                 "--solver", "thermo-accelerated", "--report", os.fspath(report)],
                cwd=ROOT, env=sandbox.env(WFC_PYTHON=os.fspath(THERMO_PYTHON)),
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(payload["backend"], "thrml-jax")
        self.assertEqual(payload["thermo"]["sampler"], "thrml")


if __name__ == "__main__":
    unittest.main()
