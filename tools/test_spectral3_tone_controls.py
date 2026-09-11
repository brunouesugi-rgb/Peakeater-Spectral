#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import pathlib
import struct
import subprocess
import tempfile


CONTROL_PAIRS = {
    "saturation": ("saturation_min", "saturation_max", 0.020),
    "density": ("density_min", "density_max", 0.006),
    "sat_tone": ("sat_tone_min", "sat_tone_max", 0.008),
    "bass_safe": ("bass_safe_min", "bass_safe_max", 0.006),
    "detector_hp": ("detector_hp_min", "detector_hp_max", 0.006),
    "attack": ("attack_min", "attack_max", 0.006),
    "hold": ("hold_min", "hold_max", 0.004),
    "release": ("release_min", "release_max", 0.006),
    "transient": ("transient_min", "transient_max", 0.006),
}


def render(executable: pathlib.Path, profile: str, output: pathlib.Path) -> list[float]:
    subprocess.run([str(executable), "1", str(output), profile], check=True)
    payload = output.read_bytes()
    if len(payload) % 4:
        raise RuntimeError(f"invalid float32 render size: {len(payload)}")
    return [value[0] for value in struct.iter_unpack("<f", payload)]


def peak(samples: list[float]) -> float:
    return max((abs(sample) for sample in samples), default=0.0)


def rms(samples: list[float]) -> float:
    return math.sqrt(sum(sample * sample for sample in samples) / max(1, len(samples)))


def normalized_delta(left: list[float], right: list[float]) -> float:
    error = math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)) / max(1, len(left)))
    reference = max(1.0e-9, 0.5 * (rms(left) + rms(right)))
    return error / reference


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("benchmark", type=pathlib.Path)
    args = parser.parse_args()

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="peakeater-tone-controls-") as folder:
        root = pathlib.Path(folder)
        for name, (minimum, maximum, required_delta) in CONTROL_PAIRS.items():
            low = render(args.benchmark, minimum, root / f"{minimum}.raw")
            high = render(args.benchmark, maximum, root / f"{maximum}.raw")
            if not all(math.isfinite(sample) for sample in low + high):
                failures.append(f"{name}: non-finite output")
                continue
            delta = normalized_delta(low, high)
            max_peak = max(peak(low), peak(high))
            print(f"{name:12s} delta={delta:.6f} peak={max_peak:.6f}")
            if delta < required_delta:
                failures.append(f"{name}: delta {delta:.6f} < {required_delta:.6f}")
            if max_peak > 1.0 + 1.0e-5:
                failures.append(f"{name}: peak {max_peak:.6f} exceeds 0 dBFS")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: Spectral 3 Tone & Sat controls have measurable, peak-safe range")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
