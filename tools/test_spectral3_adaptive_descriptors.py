#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def metric(output: str, name: str) -> int:
    match = re.search(rf"{name}=([0-9]+)", output)
    if match is None:
        raise RuntimeError(f"Missing {name} in benchmark output:\n{output}")
    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--band-multiplier", type=int, default=16)
    args = parser.parse_args()

    completed = subprocess.run(
        [str(args.executable), str(args.band_multiplier)],
        check=True,
        capture_output=True,
        text=True,
    )
    output = completed.stdout + completed.stderr
    updates = metric(output, "descriptor_updates")
    event_updates = metric(output, "descriptor_event_updates")
    processed_samples = (256 + (5 * 5000)) * 128 * 2
    fixed_interval = 2 if args.band_multiplier > 4 else 24
    fixed_updates = processed_samples // fixed_interval

    failures: list[str] = []
    if updates >= int(fixed_updates * 0.82):
        failures.append(f"adaptive updates {updates} did not beat fixed cadence {fixed_updates}")
    if event_updates <= 0:
        failures.append("no transient-triggered descriptor updates were observed")

    print(f"descriptor_updates={updates}")
    print(f"descriptor_event_updates={event_updates}")
    print(f"fixed_descriptor_updates={fixed_updates}")
    if failures:
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
