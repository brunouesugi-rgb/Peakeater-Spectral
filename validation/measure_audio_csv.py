#!/usr/bin/env python3
import argparse
import csv
import math
import struct
import wave
from pathlib import Path


MIN_DB = -120.0


def db_from_power(power):
    if power <= 0.0:
        return MIN_DB
    return max(MIN_DB, -0.691 + (10.0 * math.log10(power)))


def db_from_gain(gain):
    if gain <= 0.0:
        return MIN_DB
    return max(MIN_DB, 20.0 * math.log10(gain))


def read_wav(path):
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_rate = wav.getframerate()
        sample_width = wav.getsampwidth()
        frames = wav.getnframes()
        raw = wav.readframes(frames)

    if sample_width not in (2, 3, 4):
        raise ValueError(f"{path}: only 16/24/32-bit PCM WAV is supported")

    samples = [[] for _ in range(channels)]
    stride = sample_width * channels
    for frame_offset in range(0, len(raw), stride):
        for channel in range(channels):
            offset = frame_offset + (channel * sample_width)
            if sample_width == 2:
                value = struct.unpack_from("<h", raw, offset)[0] / 32768.0
            elif sample_width == 3:
                b = raw[offset : offset + 3]
                integer = int.from_bytes(b + (b"\xff" if b[2] & 0x80 else b"\x00"), "little", signed=True)
                value = integer / 8388608.0
            else:
                value = struct.unpack_from("<i", raw, offset)[0] / 2147483648.0
            samples[channel].append(max(-1.5, min(1.5, value)))

    return samples, sample_rate


def true_peak_4x(samples):
    taps = (
        (-0.032, 0.265, 0.900, -0.133),
        (-0.0625, 0.5625, 0.5625, -0.0625),
        (-0.133, 0.900, 0.265, -0.032),
    )
    peak = 0.0
    for channel in samples:
        if not channel:
            continue
        for i, current in enumerate(channel):
            previous2 = channel[i - 2] if i > 1 else current
            previous = channel[i - 1] if i > 0 else current
            next_sample = channel[i + 1] if i + 1 < len(channel) else current
            peak = max(peak, abs(current))
            for phase in taps:
                oversampled = (
                    previous2 * phase[0]
                    + previous * phase[1]
                    + current * phase[2]
                    + next_sample * phase[3]
                )
                peak = max(peak, abs(oversampled))
    return db_from_gain(peak)


def window_lufs(mono, sample_rate, window_seconds):
    window = max(1, int(sample_rate * window_seconds))
    if len(mono) < window:
        power = sum(x * x for x in mono) / max(1, len(mono))
        return db_from_power(power), db_from_power(power)

    max_lufs = MIN_DB
    last_lufs = MIN_DB
    hop = max(1, int(sample_rate * 0.1))
    running = sum(x * x for x in mono[:window])
    for start in range(0, len(mono) - window + 1, hop):
        if start > 0:
            prev = start - hop
            for i in range(prev, min(start, len(mono))):
                running -= mono[i] * mono[i]
            for i in range(prev + window, min(start + window, len(mono))):
                running += mono[i] * mono[i]
        last_lufs = db_from_power(running / window)
        max_lufs = max(max_lufs, last_lufs)
    return max_lufs, last_lufs


def integrated_lufs(mono, sample_rate):
    block = max(1, int(sample_rate * 0.4))
    powers = []
    for start in range(0, len(mono), block):
        chunk = mono[start : start + block]
        if not chunk:
            continue
        power = sum(x * x for x in chunk) / len(chunk)
        if db_from_power(power) > -70.0:
            powers.append((power, len(chunk)))
    if not powers:
        return MIN_DB

    ungated = sum(power * count for power, count in powers) / sum(count for _, count in powers)
    relative_gate = db_from_power(ungated) - 10.0
    gated = [(power, count) for power, count in powers if db_from_power(power) > relative_gate]
    if not gated:
        gated = powers
    return db_from_power(sum(power * count for power, count in gated) / sum(count for _, count in gated))


def stereo_correlation(samples):
    if len(samples) < 2:
        return 1.0
    left, right = samples[0], samples[1]
    count = min(len(left), len(right))
    if count == 0:
        return 0.0
    lr = sum(left[i] * right[i] for i in range(count))
    ll = sum(left[i] * left[i] for i in range(count))
    rr = sum(right[i] * right[i] for i in range(count))
    denom = math.sqrt(max(1e-20, ll * rr))
    return max(-1.0, min(1.0, lr / denom))


def analyze(path):
    samples, sample_rate = read_wav(path)
    frame_count = max((len(ch) for ch in samples), default=0)
    duration = frame_count / sample_rate if sample_rate else 0.0
    mono = [
        sum(ch[i] for ch in samples if i < len(ch)) / max(1, len(samples))
        for i in range(frame_count)
    ]
    peak = max((abs(x) for ch in samples for x in ch), default=0.0)
    rms_power = sum(x * x for x in mono) / max(1, len(mono))
    rms_gain = math.sqrt(max(0.0, rms_power))
    peak_db = db_from_gain(peak)
    rms_db = db_from_gain(rms_gain)
    crest_db = max(0.0, peak_db - rms_db)
    lufs_m_max, _ = window_lufs(mono, sample_rate, 0.4)
    _, lufs_s = window_lufs(mono, sample_rate, 3.0)
    lufs_i = integrated_lufs(mono, sample_rate)
    mono_peak = max((abs(x) for x in mono), default=0.0)
    mono_delta_db = db_from_gain(mono_peak) - peak_db
    dr_like = round(max(0.0, crest_db - 3.0))

    return {
        "file": str(path),
        "duration": f"{duration:.3f}",
        "sample_rate": sample_rate,
        "peak_dbfs": f"{peak_db:.2f}",
        "true_peak_dbtp": f"{true_peak_4x(samples):.2f}",
        "rms_db": f"{rms_db:.2f}",
        "crest_db": f"{crest_db:.2f}",
        "lufs_m_max": f"{lufs_m_max:.2f}",
        "lufs_s": f"{lufs_s:.2f}",
        "lufs_i": f"{lufs_i:.2f}",
        "clip_max": "",
        "gr_max": "",
        "stereo_correlation": f"{stereo_correlation(samples):.3f}",
        "mono_delta_db": f"{mono_delta_db:.2f}",
        "dr_like_score": dr_like,
        "reaper_cpu": "",
        "reaper_rt_cpu": "",
        "render_time": "",
        "automation_clicks": "",
        "notes": "",
    }


def main():
    parser = argparse.ArgumentParser(description="Measure WAV files for Peakeater Spectral validation and write CSV.")
    parser.add_argument("paths", nargs="+", help="WAV files or folders containing WAV files")
    parser.add_argument("-o", "--output", default="validation-results.csv", help="CSV output path")
    args = parser.parse_args()

    wavs = []
    for item in args.paths:
        path = Path(item)
        if path.is_dir():
            wavs.extend(sorted(path.rglob("*.wav")))
        elif path.suffix.lower() == ".wav":
            wavs.append(path)

    fieldnames = [
        "file", "duration", "sample_rate", "peak_dbfs", "true_peak_dbtp", "rms_db", "crest_db",
        "lufs_m_max", "lufs_s", "lufs_i", "clip_max", "gr_max", "stereo_correlation",
        "mono_delta_db", "dr_like_score", "reaper_cpu", "reaper_rt_cpu", "render_time",
        "automation_clicks", "notes",
    ]

    rows = [analyze(path) for path in wavs]
    with open(args.output, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {len(rows)} rows to {args.output}")


if __name__ == "__main__":
    main()
