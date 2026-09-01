#!/usr/bin/env python3
"""Compare quality and solve cost across the portable and accelerated paths."""

import argparse
import json
import os
import subprocess
import statistics
import sys
import tempfile
import time
from pathlib import Path


MODES = ("streets", "neurons", "mycelium", "delta", "rail")
SOLVERS = ("classic", "thermo-ephemeral", "thermo-learned", "thermo-accelerated")


def run_case(binary, mode, solver, trial, width, height, profile_dir):
    seed = 7001 + trial * 7919 + MODES.index(mode) * 101
    report = Path(profile_dir) / ("report-%s-%s-%d.json" % (mode, solver, trial))
    run_profile = Path(profile_dir) / mode / solver / ("trial-%d" % trial)
    args = [
        str(binary), "--mode", mode, "--seed", str(seed),
        "--w", str(width), "--h", str(height), "--once",
        "--report", str(report), "--solver",
        "classic" if solver == "classic" else
        "thermo-accelerated" if solver == "thermo-accelerated" else "thermo",
    ]
    if solver != "classic":
        run_profile.mkdir(parents=True, exist_ok=True)
        args.extend(["--thermo-profile", str(run_profile)])
    if solver == "thermo-ephemeral":
        args.append("--no-learn")
    started = time.monotonic()
    try:
        env = os.environ.copy()
        env["HOME"] = profile_dir          # never touch the user's ~/.wfcrc
        result = subprocess.run(
            args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=60, env=env,
        )
        elapsed_ms = (time.monotonic() - started) * 1000.0
    except subprocess.TimeoutExpired as error:
        return {
            "mode": mode, "solver": solver, "trial": trial, "seed": seed,
            "success": False, "quality": 0.0, "ms": 60000.0,
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
        "backend": payload.get("backend") if isinstance(payload, dict) else None,
        "sampler": payload.get("thermo", {}).get("sampler") if isinstance(payload, dict) else None,
        **({"error": error} if result.returncode != 0 or not payload else {}),
    }


def percentile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize(results):
    summary = {}
    for solver in SOLVERS:
        rows = [item for item in results if item["solver"] == solver]
        times = [float(item["ms"]) for item in rows]
        qualities = [float(item["quality"]) for item in rows]
        summary[solver] = {
            "cases": len(rows),
            "successes": sum(item["success"] for item in rows),
            "median_ms": round(statistics.median(times), 3),
            "p95_ms": round(percentile(times, 0.95), 3),
            "median_quality": round(statistics.median(qualities), 6),
        }
    return summary


def write_json_artifact(path, payload):
    destination = Path(path)
    temporary = destination.with_name(destination.name + ".tmp")
    try:
        temporary.write_text(payload + "\n", encoding="utf-8")
        temporary.replace(destination)
    except OSError:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./wfc", type=Path)
    parser.add_argument("--trials", default=2, type=int)
    parser.add_argument("--w", default=8, type=int)
    parser.add_argument("--h", default=6, type=int)
    parser.add_argument("--json-out", type=Path,
                        help="also write the final JSON report atomically")
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
    summary = summarize(results)
    print("solver             median-ms  p95-ms  median-quality")
    for solver in SOLVERS:
        item = summary[solver]
        print("%-18s %9.1f %7.1f %15.3f" % (
            solver, item["median_ms"], item["p95_ms"], item["median_quality"],
        ))
    payload = {
        "schema": 3,
        "trials": args.trials,
        "dimensions": {"w": args.w, "h": args.h},
        "solvers": list(SOLVERS),
        "modes": list(MODES),
        "results": results,
        "summary": summary,
    }
    serialized = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    if args.json_out:
        try:
            write_json_artifact(args.json_out, serialized)
        except OSError as error:
            print("benchmark: failed to write %s: %s" % (args.json_out, error),
                  file=sys.stderr)
            return 1
    print(serialized)
    return 0 if all(item["success"] for item in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
