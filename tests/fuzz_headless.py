#!/usr/bin/env python3
"""Run deterministic ASan headless cases and print exact replay inputs."""

import argparse
import os
import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sandbox  # noqa: E402


def generate_cases(modes, count, seed):
    rng = random.Random(seed)
    names = tuple(modes)
    cases = []
    for _ in range(count):
        cases.append({
            "mode": rng.choice(names),
            "seed": rng.getrandbits(32),
            "w": rng.randint(6, 64),
            "h": rng.randint(6, 40),
            "pan": rng.randrange(3) == 0,
            "no_bloom": rng.randrange(3) == 0,
            "no_weather": rng.randrange(3) == 0,
            "zoom": 3 if rng.randrange(4) == 0 else 1,
        })
    return cases


def command_for(binary, case):
    command = [
        os.fspath(binary), "--mode", case["mode"], "--seed", str(case["seed"]),
        "--w", str(case["w"]), "--h", str(case["h"]), "--once",
    ]
    for key, flag in (("pan", "--pan"), ("no_bloom", "--no-bloom"),
                      ("no_weather", "--no-weather")):
        if case[key]:
            command.append(flag)
    if case["zoom"] != 1:
        command.extend(["--zoom", str(case["zoom"])])
    return command


def run_case(binary, case, timeout):
    result = subprocess.run(
        command_for(binary, case), cwd=ROOT, env=sandbox.env(
            ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
        ), capture_output=True, text=True, timeout=timeout,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "case failed: %s\nstdout:\n%s\nstderr:\n%s" % (
                case, result.stdout[-2000:], result.stderr[-4000:],
            )
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "wfc_asan")
    parser.add_argument("--runs", type=int, default=50)
    parser.add_argument("--seed", type=int, default=20260831)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    if args.runs < 1 or args.runs > 500:
        parser.error("--runs must be in the range 1..500")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if not args.binary.exists():
        parser.error("binary does not exist: %s" % args.binary)
    args.binary = args.binary.resolve()
    modes = subprocess.check_output(
        [os.fspath(ROOT / "wfc"), "--list-modes"], cwd=ROOT, text=True,
    ).split()
    cases = generate_cases(modes, args.runs, args.seed)
    for index, case in enumerate(cases, 1):
        print("fuzz: seed=%d case=%d/%d %s" % (args.seed, index, args.runs, case),
              flush=True)
        try:
            run_case(args.binary, case, args.timeout)
        except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
            print("fuzz: replay with --seed %d and case %s" % (args.seed, case),
                  file=sys.stderr)
            print("fuzz: %s" % error, file=sys.stderr)
            return 1
    print("fuzz: %d deterministic cases clean (seed=%d)" % (args.runs, args.seed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
