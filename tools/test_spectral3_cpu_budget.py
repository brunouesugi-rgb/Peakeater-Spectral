#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


BASELINES = {
    1: {"realtime_percent": 14.6156, "output_hash": 6401862609481837694},
    16: {"realtime_percent": 53.0937, "output_hash": 1394364625663002285},
}


def run_benchmark(executable: Path, band_multiplier: int) -> tuple[float, int]:
    completed = subprocess.run(
        [str(executable), str(band_multiplier)],
        check=True,
        capture_output=True,
        text=True,
    )
    output = completed.stdout + completed.stderr
    cpu_match = re.search(r"realtime_percent=([0-9.]+)", output)
    hash_match = re.search(r"output_hash=([0-9]+)", output)
    if cpu_match is None or hash_match is None:
        raise RuntimeError(f"Could not parse benchmark output:\n{output}")
    return float(cpu_match.group(1)), int(hash_match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--minimum-reduction", type=float, default=5.0)
    args = parser.parse_args()

    failures: list[str] = []
    for band_multiplier, baseline in BASELINES.items():
        cpu, output_hash = run_benchmark(args.executable, band_multiplier)
        reduction = (baseline["realtime_percent"] - cpu) / baseline["realtime_percent"] * 100.0
        print(
            f"band_multiplier={band_multiplier} cpu={cpu:.4f}% "
            f"reduction={reduction:.2f}% output_hash={output_hash}"
        )
        if output_hash != baseline["output_hash"]:
            failures.append(f"band_multiplier={band_multiplier}: output hash changed")
        if reduction < args.minimum_reduction:
            failures.append(
                f"band_multiplier={band_multiplier}: reduction {reduction:.2f}% "
                f"is below {args.minimum_reduction:.2f}%"
            )

    if failures:
        print("CPU budget gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
