#!/usr/bin/env python3
"""Run the quality benchmark and enforce absolute and trend SLOs."""

import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK = ROOT / "tests" / "quality_benchmark.py"


def _violation(solver, metric, rule, actual, limit):
    return {
        "solver": solver,
        "metric": metric,
        "rule": rule,
        "actual": actual,
        "limit": limit,
    }


def _finite_float(value):
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _integer(value):
    if isinstance(value, bool):
        return None
    try:
        number = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    if isinstance(value, float) and number != value:
        return None
    return number


def compare(current, budget, baseline=None):
    violations = []
    expected = budget.get("benchmark", {})
    for key in ("trials", "w", "h"):
        if key not in expected:
            continue
        actual = current.get("trials") if key == "trials" else current.get(
            "dimensions", {}).get(key)
        if actual != expected[key]:
            violations.append(_violation("benchmark", key, "configuration",
                                         actual, expected[key]))

    summary = current.get("summary", {})
    baseline_summary = baseline.get("summary", {}) if baseline else {}
    trend = budget.get("trend", {})
    max_ratio = float(trend.get("max_p95_ratio", 1.75))
    max_drop = float(trend.get("max_quality_drop", 0.02))
    for solver, limits in budget.get("solvers", {}).items():
        row = summary.get(solver)
        if not isinstance(row, dict):
            violations.append(_violation(solver, "summary", "missing", None, None))
            continue
        raw_successes = row.get("successes")
        successes = _integer(raw_successes)
        min_successes = _integer(limits.get("min_successes", row.get("cases", 0)))
        if successes is None:
            violations.append(_violation(solver, "successes", "invalid",
                                         raw_successes, min_successes))
        elif min_successes is not None and successes < min_successes:
            violations.append(_violation(solver, "successes", "min",
                                         successes, min_successes))
        raw_p95_ms = row.get("p95_ms")
        p95_ms = _finite_float(raw_p95_ms)
        max_p95_ms = _finite_float(limits.get("max_p95_ms", float("inf")))
        if p95_ms is None:
            violations.append(_violation(solver, "p95_ms", "invalid",
                                         raw_p95_ms, max_p95_ms))
        elif max_p95_ms is not None and p95_ms > max_p95_ms:
            violations.append(_violation(solver, "p95_ms", "max",
                                         p95_ms, max_p95_ms))
        raw_median_quality = row.get("median_quality")
        median_quality = _finite_float(raw_median_quality)
        min_quality = _finite_float(limits.get("min_median_quality", 0.0))
        if median_quality is None:
            violations.append(_violation(solver, "median_quality", "invalid",
                                         raw_median_quality, min_quality))
        elif min_quality is not None and median_quality < min_quality:
            violations.append(_violation(solver, "median_quality", "min",
                                         median_quality, min_quality))

        old = baseline_summary.get(solver)
        if not isinstance(old, dict):
            continue
        old_p95 = _finite_float(old.get("p95_ms"))
        if old_p95 is None:
            violations.append(_violation(solver, "baseline_p95_ms", "invalid",
                                         old.get("p95_ms"), None))
        elif p95_ms is not None and old_p95 > 0.0 and p95_ms > old_p95 * max_ratio:
            violations.append(_violation(solver, "p95_ms", "trend",
                                         p95_ms, old_p95 * max_ratio))
        old_quality = _finite_float(old.get("median_quality"))
        if old_quality is None:
            violations.append(_violation(solver, "baseline_median_quality", "invalid",
                                         old.get("median_quality"), None))
        elif median_quality is not None and median_quality < old_quality - max_drop:
            violations.append(_violation(solver, "median_quality", "trend",
                                         median_quality, old_quality - max_drop))
    return violations


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default=ROOT / "wfc", type=Path)
    parser.add_argument("--budget", default=ROOT / "tests" / "performance_budget.json",
                        type=Path)
    parser.add_argument("--baseline", type=Path,
                        help="compare p95/quality against a previous JSON report")
    parser.add_argument("--save-current", type=Path,
                        help="keep the current JSON report at this path")
    parser.add_argument("--trials", default=2, type=int)
    parser.add_argument("--w", default=8, type=int)
    parser.add_argument("--h", default=6, type=int)
    args = parser.parse_args()
    if not args.binary.exists():
        parser.error("binary does not exist: %s" % args.binary)
    if not args.budget.exists():
        parser.error("budget does not exist: %s" % args.budget)
    if args.baseline and not args.baseline.exists():
        parser.error("baseline does not exist: %s" % args.baseline)

    with tempfile.TemporaryDirectory(prefix="wfc-perf-gate-") as temp_dir:
        current_path = args.save_current or Path(temp_dir) / "current.json"
        command = [
            sys.executable, str(BENCHMARK), "--binary", str(args.binary),
            "--trials", str(args.trials), "--w", str(args.w), "--h", str(args.h),
            "--json-out", str(current_path),
        ]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        if result.stderr:
            sys.stderr.write(result.stderr)
        if not current_path.exists():
            print("PERF GATE FAIL: benchmark did not produce a JSON artifact",
                  file=sys.stderr)
            return 1
        try:
            current = read_json(current_path)
            budget = read_json(args.budget)
            baseline = read_json(args.baseline) if args.baseline else None
        except (OSError, ValueError) as error:
            print("PERF GATE FAIL: invalid benchmark data: %s" % error,
                  file=sys.stderr)
            return 1
        violations = compare(current, budget, baseline)

    verdict = {
        "schema": 1,
        "status": "fail" if result.returncode != 0 or violations else "pass",
        "violations": violations,
    }
    for item in violations:
        print("PERF FAIL solver=%s metric=%s rule=%s actual=%s limit=%s" % (
            item["solver"], item["metric"], item["rule"],
            item["actual"], item["limit"],
        ))
    if result.returncode != 0:
        print("PERF FAIL benchmark command returned %d" % result.returncode)
    if result.returncode == 0 and not violations:
        print("PERF GATE PASS")
    print(json.dumps(verdict, sort_keys=True, separators=(",", ":")))
    return 0 if verdict["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
