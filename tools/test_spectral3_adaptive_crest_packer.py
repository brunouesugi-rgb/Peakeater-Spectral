import argparse
import array
import math
import pathlib


def measure(path: pathlib.Path) -> tuple[float, float, float]:
    samples = array.array("f")
    with path.open("rb") as stream:
        samples.fromfile(stream, path.stat().st_size // samples.itemsize)
    if not samples or any(not math.isfinite(sample) for sample in samples):
        raise ValueError(f"invalid float audio: {path}")
    peak = max(abs(sample) for sample in samples)
    rms = math.sqrt(sum(float(sample) * sample for sample in samples) / len(samples))
    peak_db = 20.0 * math.log10(max(peak, 1.0e-12))
    rms_db = 20.0 * math.log10(max(rms, 1.0e-12))
    return peak_db, rms_db, peak_db - rms_db


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--minimum-rms-lift-db", type=float, default=0.07)
    parser.add_argument("--peak-tolerance-db", type=float, default=0.005)
    args = parser.parse_args()

    baseline_peak, baseline_rms, baseline_crest = measure(args.baseline)
    candidate_peak, candidate_rms, candidate_crest = measure(args.candidate)
    rms_lift = candidate_rms - baseline_rms
    peak_delta = candidate_peak - baseline_peak
    crest_reduction = baseline_crest - candidate_crest

    print(f"baseline_peak_db={baseline_peak:.6f}")
    print(f"candidate_peak_db={candidate_peak:.6f}")
    print(f"peak_delta_db={peak_delta:.6f}")
    print(f"baseline_rms_db={baseline_rms:.6f}")
    print(f"candidate_rms_db={candidate_rms:.6f}")
    print(f"rms_lift_db={rms_lift:.6f}")
    print(f"crest_reduction_db={crest_reduction:.6f}")

    if rms_lift < args.minimum_rms_lift_db:
        raise AssertionError(
            f"RMS lift {rms_lift:.6f} dB is below {args.minimum_rms_lift_db:.6f} dB"
        )
    if peak_delta > args.peak_tolerance_db:
        raise AssertionError(
            f"Peak delta {peak_delta:.6f} dB exceeds {args.peak_tolerance_db:.6f} dB"
        )
    if crest_reduction < args.minimum_rms_lift_db - args.peak_tolerance_db:
        raise AssertionError("Crest reduction does not track the RMS lift")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
