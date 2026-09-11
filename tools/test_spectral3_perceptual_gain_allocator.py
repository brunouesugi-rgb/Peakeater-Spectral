#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import subprocess
import sys
import tempfile
from pathlib import Path


def render(benchmark: Path, output: Path, profile: str) -> dict[str, float]:
    completed = subprocess.run(
        [str(benchmark), "16", str(output), profile],
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        key: float(value)
        for line in completed.stdout.splitlines()
        for key, separator, value in [line.partition("=")]
        if separator
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("benchmark", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="peakeater-allocator-") as folder:
        root = Path(folder)
        baseline = render(args.benchmark, root / "baseline.raw", "_allocator_off")
        candidate = render(args.benchmark, root / "candidate.raw", "")

    rms_lift_db = 20.0 * math.log10(max(candidate["rms"], 1.0e-12) / max(baseline["rms"], 1.0e-12))
    peak_delta_db = 20.0 * math.log10(max(candidate["peak"], 1.0e-12) / max(baseline["peak"], 1.0e-12))
    candidate_peak_db = 20.0 * math.log10(max(candidate["peak"], 1.0e-12))
    clip_delta_db = candidate["average_clip_db"] - baseline["average_clip_db"]
    allocator_updates = int(candidate.get("perceptual_allocator_updates", 0.0))

    print(f"perceptual_allocator_updates={allocator_updates}")
    print(f"rms_lift_db={rms_lift_db:.6f}")
    print(f"peak_delta_db={peak_delta_db:.6f}")
    print(f"candidate_peak_db={candidate_peak_db:.6f}")
    print(f"clip_delta_db={clip_delta_db:.6f}")

    failures: list[str] = []
    if allocator_updates <= 0:
        failures.append("perceptual allocator did not update")
    if rms_lift_db < 0.01:
        failures.append(f"RMS lift {rms_lift_db:.6f} dB is below 0.010000 dB")
    if candidate_peak_db > -0.99:
        failures.append(f"candidate peak {candidate_peak_db:.6f} dBFS exceeds the -1 dB ceiling tolerance")
    if peak_delta_db > rms_lift_db + 0.005:
        failures.append("crest factor increased instead of packing the available peak budget")
    if clip_delta_db > 0.05:
        failures.append(f"average clip delta {clip_delta_db:.6f} dB exceeds 0.050000 dB")

    if failures:
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
