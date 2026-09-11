#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
# --- How to run ---
# uv run tools/test_drive_clip_balance.py build/Release/peakeater_cpu_benchmark.exe

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import subprocess
import sys
import tempfile


@dataclass(frozen=True, slots=True)
class ProfileExpectation:
    name: str
    max_clip_db: float
    min_rms: float


@dataclass(frozen=True, slots=True)
class RenderMetrics:
    clip_db: float
    gain_reduction_db: float
    peak: float
    rms: float


EXPECTATIONS = (
    ProfileExpectation("drive_clip_edm", 7.81, 0.7553),
    ProfileExpectation("drive_clip_hiphop", 5.75, 0.6667),
    ProfileExpectation("drive_clip_dubstep", 8.95, 0.7065),
    ProfileExpectation("drive_clip_drumnbass", 6.70, 0.7336),
    ProfileExpectation("drive_clip_trap", 7.19, 0.6923),
    ProfileExpectation("drive_clip_house", 5.60, 0.6562),
    ProfileExpectation("drive_clip_drums", 5.49, 0.5788),
    ProfileExpectation("drive_clip_one_shot", 6.08, 0.5157),
    ProfileExpectation("drive_clip_one_shot_clean", 0.60, 0.4521),
    ProfileExpectation("drive_clip_percs", 7.05, 0.6000),
    ProfileExpectation("drive_clip_bass", 4.55, 0.5243),
    ProfileExpectation("drive_clip_kick808", 2.90, 0.4985),
    ProfileExpectation("drive_clip_bright", 3.00, 0.4704),
    ProfileExpectation("drive_clip_glue", 1.60, 0.4370),
    ProfileExpectation("drive_clip_acoustic", 0.85, 0.4210),
    ProfileExpectation("drive_clip_vocal", 1.80, 0.4530),
    ProfileExpectation("drive_clip_clean", 0.288, 0.4124),
)


def render(executable: Path, profile: str, output: Path) -> RenderMetrics:
    completed = subprocess.run(
        [str(executable), "1", str(output), profile],
        check=True,
        capture_output=True,
        text=True,
    )
    values: dict[str, float] = {}
    for line in completed.stdout.splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = float(value)
    return RenderMetrics(
        clip_db=values["average_clip_db"],
        gain_reduction_db=values["gain_reduction_db"],
        peak=values["peak"],
        rms=values["rms"],
    )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_drive_clip_balance.py <benchmark>")
        return 2

    benchmark = Path(sys.argv[1]).resolve()
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="peakeater-drive-clip-") as folder:
        output_root = Path(folder)
        for expectation in EXPECTATIONS:
            metrics = render(benchmark, expectation.name, output_root / f"{expectation.name}.raw")
            print(
                f"{expectation.name}: clip={metrics.clip_db:.3f}dB "
                f"gr={metrics.gain_reduction_db:.3f}dB peak={metrics.peak:.6f} rms={metrics.rms:.6f}"
            )
            if not all(
                math.isfinite(value)
                for value in (metrics.clip_db, metrics.gain_reduction_db, metrics.peak, metrics.rms)
            ):
                failures.append(f"{expectation.name}: non-finite metric")
            if metrics.clip_db > expectation.max_clip_db:
                failures.append(
                    f"{expectation.name}: clip {metrics.clip_db:.3f}dB exceeds {expectation.max_clip_db:.3f}dB"
                )
            if metrics.rms < expectation.min_rms:
                failures.append(f"{expectation.name}: rms {metrics.rms:.6f} is below {expectation.min_rms:.6f}")
            if metrics.peak > 1.0 + 1.0e-5:
                failures.append(f"{expectation.name}: peak {metrics.peak:.6f} exceeds 0 dBFS")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: Drive clip is reduced while RMS and peak safety are preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
