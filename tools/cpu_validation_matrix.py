#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path


FIELDS = [
    "plugin_variant",
    "quality",
    "type",
    "sample_rate",
    "buffer_size",
    "instance_count",
    "average_cpu",
    "peak_cpu",
    "rt_cpu",
    "xrun_or_dropout",
    "latency_samples",
    "analyzer_open",
    "drywet",
    "drive_db",
    "clip_count",
    "true_peak_est_db",
    "rms_db",
    "lufs_like",
    "crest_db",
    "high_band_energy",
    "low_mono_risk",
    "notes",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Append a CPU validation row or create a blank CSV.")
    parser.add_argument("--csv", default="docs/peakeater_cpu_validation_matrix.csv", help="CSV path to create or append.")
    parser.add_argument("--append", action="store_true", help="Append one measurement row.")
    for field in FIELDS:
        parser.add_argument(f"--{field.replace('_', '-')}", default="", help=f"Value for {field}.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    csv_path = Path(args.csv)
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    exists = csv_path.exists() and csv_path.stat().st_size > 0

    with csv_path.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        if args.append:
            row = {field: getattr(args, field) for field in FIELDS}
            writer.writerow(row)

    print(csv_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
