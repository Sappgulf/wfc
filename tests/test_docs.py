import subprocess
import unittest
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DocumentationContractTests(unittest.TestCase):
    def test_readme_mode_contract_matches_the_registry(self):
        modes = subprocess.check_output(
            [str(ROOT / "wfc"), "--list-modes"], cwd=ROOT, text=True,
        ).split()
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("./wfc --list-modes", readme)
        self.assertIn("--inspect-world", readme)
        self.assertIn("make perf-check", readme)
        self.assertIn("all 37 modes", readme)
        self.assertNotIn("all 25 modes", readme)
        for mode in modes:
            self.assertRegex(readme, r"\|\s*%s\s*\|" % re.escape(mode))


if __name__ == "__main__":
    unittest.main()
