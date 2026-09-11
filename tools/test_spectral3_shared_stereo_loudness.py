#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("benchmark", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="peakeater-shared-stereo-") as folder:
        output = Path(folder) / "hq-edm.raw"
        completed = subprocess.run(
            [str(args.benchmark), "16", str(output), "drive_clip_edm"],
            check=True,
            capture_output=True,
            text=True,
        )

    metrics = {
        key: float(value)
        for line in completed.stdout.splitlines()
        for key, separator, value in [line.partition("=")]
        if separator
    }
    failures: list[str] = []
    if metrics["rms"] < 0.749000:
        failures.append(f"HQ shared stereo RMS {metrics['rms']:.6f} did not improve")
    if metrics["peak"] > 1.000010:
        failures.append(f"peak {metrics['peak']:.6f} exceeds 0 dBFS")
    if metrics["average_clip_db"] > 24.650000:
        failures.append(f"average clip {metrics['average_clip_db']:.6f} dB regressed")

    print(f"rms={metrics['rms']:.6f}")
    print(f"peak={metrics['peak']:.6f}")
    print(f"average_clip_db={metrics['average_clip_db']:.6f}")
    if failures:
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
