#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import pathlib
import struct
import subprocess
import tempfile


EXPECTED_TYPES = [
    "EDM",
    "HIP HOP",
    "DRUMS",
    "ONE SHOT",
    "ACOUSTIC",
    "VOCAL",
    "BASS",
    "BRIGHT",
    "GLUE",
    "CLEAN",
    "PERCS",
    "DUBSTEP",
    "DRUMNBASS",
    "HOUSE",
    "TRAP",
    "808&KICK",
    "ONE SHOT CLEAN",
]


def render(executable: pathlib.Path, profile: str, output: pathlib.Path) -> list[float]:
    subprocess.run([str(executable), "1", str(output), profile], check=True)
    payload = output.read_bytes()
    if len(payload) % 4:
        raise RuntimeError(f"invalid float32 render size: {len(payload)}")
    return [value[0] for value in struct.iter_unpack("<f", payload)]


def rms(samples: list[float]) -> float:
    return math.sqrt(sum(sample * sample for sample in samples) / max(1, len(samples)))


def normalized_delta(left: list[float], right: list[float]) -> float:
    error = rms([a - b for a, b in zip(left, right)])
    return error / max(1.0e-9, 0.5 * (rms(left) + rms(right)))


def source_contract(source_root: pathlib.Path) -> list[str]:
    failures: list[str] = []
    parameters = (source_root / "source/Parameters.h").read_text(encoding="utf-8")
    control_panel = (source_root / "source/editor/ControlPanel.cpp").read_text(encoding="utf-8")
    processor = (source_root / "source/PluginProcessor.cpp").read_text(encoding="utf-8")
    header = (source_root / "source/processor/SpectralMaximizer.h").read_text(encoding="utf-8")
    expected_sequence = ", ".join(f'"{name}"' for name in EXPECTED_TYPES)
    compact_parameters = " ".join(parameters.split())
    compact_panel = " ".join(control_panel.split())
    if expected_sequence not in compact_parameters:
        failures.append("Parameters Type order does not append ONE SHOT CLEAN")
    if expected_sequence not in compact_panel:
        failures.append("ControlPanel Type order does not append ONE SHOT CLEAN")
    if "case 16:" not in processor or "Mode::OneShotClean" not in processor:
        failures.append("APVTS choice 16 is not mapped to OneShotClean")
    if "OneShotClean" not in header:
        failures.append("OneShotClean enum is missing")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("benchmark", type=pathlib.Path)
    parser.add_argument("--source-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    failures = source_contract(args.source_root)
    with tempfile.TemporaryDirectory(prefix="peakeater-one-shot-clean-") as folder:
        root = pathlib.Path(folder)
        one_shot = render(args.benchmark, "mode_one_shot", root / "one-shot.raw")
        clean = render(args.benchmark, "mode_one_shot_clean", root / "one-shot-clean.raw")
        if not all(math.isfinite(sample) for sample in one_shot + clean):
            failures.append("non-finite DSP output")
        max_peak = max((abs(sample) for sample in one_shot + clean), default=0.0)
        delta = normalized_delta(one_shot, clean)
        print(f"one_shot_clean delta={delta:.6f} peak={max_peak:.6f}")
        if delta < 0.012:
            failures.append(f"One Shot Clean is not audibly distinct: delta {delta:.6f}")
        if max_peak > 1.0 + 1.0e-5:
            failures.append(f"peak {max_peak:.6f} exceeds 0 dBFS")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: One Shot Clean is appended, distinct, finite, and peak-safe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
