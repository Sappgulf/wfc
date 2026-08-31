#!/usr/bin/env python3
"""Exercise the JSONL sidecar bridge for every registered world."""

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sandbox  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "wfc_asan")
    parser.add_argument("--worker", type=Path, default=ROOT / "tests" / "fake_thermo.py")
    parser.add_argument("--w", type=int, default=8)
    parser.add_argument("--h", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()
    if not args.binary.exists() or not args.worker.exists():
        parser.error("binary and worker must exist")
    args.binary = args.binary.resolve()
    args.worker = args.worker.resolve()
    modes = subprocess.check_output(
        [os.fspath(ROOT / "wfc"), "--list-modes"], cwd=ROOT, text=True,
    ).split()
    for mode in modes:
        command = [
            os.fspath(args.binary), "--mode", mode, "--solver", "thermo",
            "--no-learn", "--once", "--w", str(args.w), "--h", str(args.h),
            "--seed", "7",
        ]
        try:
            result = subprocess.run(
                command, cwd=ROOT,
                env=sandbox.env(WFC_THERMO_PY=os.fspath(args.worker),
                                 ASAN_OPTIONS="detect_leaks=0:halt_on_error=1"),
                capture_output=True, text=True, timeout=args.timeout,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            print("thermo: %s failed: %s" % (mode, error), file=sys.stderr)
            return 1
        if result.returncode != 0:
            print("thermo: %s failed\n%s" % (mode, result.stderr[-4000:]),
                  file=sys.stderr)
            return 1
        print("thermo: %s ok" % mode, flush=True)
    print("thermo: %d deterministic mode cases clean" % len(modes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
