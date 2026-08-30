#!/usr/bin/env python3
"""Compare quality and solve cost across the three supported solver paths."""

import argparse
import json
import subprocess
import tempfile
import time
from pathlib import Path


MODES = ("streets", "neurons", "mycelium", "delta", "rail")
SOLVERS = ("classic", "thermo-ephemeral", "thermo-learned")


def run_case(binary, mode, solver, trial, width, height, profile_dir):
    seed = 7001 + trial * 7919 + MODES.index(mode) * 101
    report = Path(profile_dir) / ("report-%s-%s-%d.json" % (mode, solver, trial))
    run_profile = Path(profile_dir) / mode / solver / ("trial-%d" % trial)
    args = [
        str(binary), "--mode", mode, "--seed", str(seed),
        "--w", str(width), "--h", str(height), "--once",
        "--report", str(report), "--solver",
        "classic" if solver == "classic" else "thermo",
    ]
    if solver != "classic":
        run_profile.mkdir(parents=True, exist_ok=True)
        args.extend(["--thermo-profile", str(run_profile)])
    if solver == "thermo-ephemeral":
        args.append("--no-learn")
    started = time.monotonic()
    try:
        result = subprocess.run(
            args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=30,
        )
        elapsed_ms = (time.monotonic() - started) * 1000.0
    except subprocess.TimeoutExpired as error:
        return {
            "mode": mode, "solver": solver, "trial": trial, "seed": seed,
            "success": False, "quality": 0.0, "ms": 30000.0,
            "error": "timeout: %s" % error,
        }
    payload = None
    error = result.stderr.strip()[-320:]
    if report.exists():
        try:
            payload = json.loads(report.read_text(encoding="utf-8"))
        except (OSError, ValueError) as parse_error:
            error = "invalid report: %s" % parse_error
    quality = 0.0
    if isinstance(payload, dict):
        quality = float(payload.get("quality", {}).get("total", 0.0))
    return {
        "mode": mode, "solver": solver, "trial": trial, "seed": seed,
        "success": result.returncode == 0 and isinstance(payload, dict),
        "quality": round(quality, 6),
        "ms": round(elapsed_ms, 3),
        **({"error": error} if result.returncode != 0 or not payload else {}),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./wfc", type=Path)
    parser.add_argument("--trials", default=2, type=int)
    parser.add_argument("--w", default=8, type=int)
    parser.add_argument("--h", default=6, type=int)
    args = parser.parse_args()
    if args.trials < 1 or args.trials > 20:
        parser.error("--trials must be in the range 1..20")
    if args.w < 2 or args.h < 2 or args.w > 64 or args.h > 40:
        parser.error("--w/--h must fit the bounded benchmark grid")
    if not args.binary.exists():
        parser.error("binary does not exist: %s" % args.binary)
    args.binary = args.binary.resolve()

    results = []
    with tempfile.TemporaryDirectory(prefix="wfc-quality-bench-") as profile_dir:
        for trial in range(args.trials):
            for mode in MODES:
                for solver in SOLVERS:
                    results.append(run_case(
                        args.binary, mode, solver, trial,
                        args.w, args.h, profile_dir,
                    ))
    print("QUALITY BENCHMARK trials=%d size=%dx%d" %
          (args.trials, args.w, args.h))
    print("mode      solver             ok  quality     ms")
    for item in results:
        print("%-9s %-18s %3s %7.3f %8.1f" % (
            item["mode"], item["solver"],
            "yes" if item["success"] else "no",
            item["quality"], item["ms"],
        ))
    payload = {
        "schema": 1,
        "trials": args.trials,
        "dimensions": {"w": args.w, "h": args.h},
        "solvers": list(SOLVERS),
        "modes": list(MODES),
        "results": results,
    }
    print(json.dumps(payload, sort_keys=True, separators=(",", ":")))
    return 0 if all(item["success"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
