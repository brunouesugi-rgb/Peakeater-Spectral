from __future__ import annotations

import csv
import html
import json
import math
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, median
from typing import Any

import numpy as np
import soundfile as sf

try:
    import pyloudnorm as pyln
except Exception:  # pragma: no cover - fallback only
    pyln = None


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(r"O:\[Audio-4U] J-CORE 14 (flac)")
TARGET_CODES = [
    "DVSP-0124",
    "DVSP-0180",
    "DVSP-0225",
    "DVSP-0288",
    "DVSP-0099",
    "DVSP-0181",
    "DVSP-0087",
]

DOCS_DIR = PROJECT_ROOT / "docs"
HTML_PATH = DOCS_DIR / "peakeater_spectral_reference_analysis.html"
CSV_PATH = DOCS_DIR / "peakeater_spectral_reference_analysis.csv"
JSON_PATH = DOCS_DIR / "peakeater_spectral_reference_analysis.json"

BANDS = [
    ("20_80_hz", 20.0, 80.0),
    ("80_160_hz", 80.0, 160.0),
    ("160_300_hz", 160.0, 300.0),
    ("300_1k_hz", 300.0, 1000.0),
    ("1k_4k_hz", 1000.0, 4000.0),
    ("4k_8k_hz", 4000.0, 8000.0),
    ("8k_16k_hz", 8000.0, 16000.0),
    ("16k_plus_hz", 16000.0, 48000.0),
]

USE_FFMPEG_LOUDNORM = False


def configure_console() -> None:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def db_from_linear(value: float) -> float:
    if not math.isfinite(value) or value <= 0.0:
        return -120.0
    return 20.0 * math.log10(max(value, 1.0e-12))


def db_from_power(value: float) -> float:
    if not math.isfinite(value) or value <= 0.0:
        return -120.0
    return 10.0 * math.log10(max(value, 1.0e-24))


def clean_float(value: Any, default: float = float("nan")) -> float:
    try:
        out = float(value)
        return out if math.isfinite(out) else default
    except Exception:
        return default


def find_target_albums() -> list[Path]:
    if not SOURCE_ROOT.exists():
        raise FileNotFoundError(f"Source root not found: {SOURCE_ROOT}")

    albums: list[Path] = []
    children = [p for p in SOURCE_ROOT.iterdir() if p.is_dir()]
    for code in TARGET_CODES:
        matches = [p for p in children if code in p.name]
        if not matches:
            print(f"WARNING: no album folder found for {code}")
            continue
        albums.append(sorted(matches, key=lambda p: p.name)[0])
    return albums


def run_loudnorm(path: Path) -> dict[str, float]:
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-nostats",
        "-i",
        str(path),
        "-af",
        "loudnorm=I=-14:TP=-1:LRA=11:print_format=json",
        "-f",
        "null",
        "NUL",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    stderr = proc.stderr or ""
    match = re.search(r"\{\s*\"input_i\".*?\}", stderr, re.DOTALL)
    if proc.returncode != 0 or not match:
        return {}
    try:
        data = json.loads(match.group(0))
    except json.JSONDecodeError:
        return {}
    return {
        "integrated_lufs": clean_float(data.get("input_i")),
        "true_peak_dbtp": clean_float(data.get("input_tp")),
        "lra": clean_float(data.get("input_lra")),
        "loudnorm_thresh": clean_float(data.get("input_thresh")),
    }


def fallback_integrated_lufs(audio: np.ndarray, sample_rate: int) -> float:
    if pyln is None:
        return float("nan")
    try:
        meter = pyln.Meter(sample_rate)
        return float(meter.integrated_loudness(audio))
    except Exception:
        return float("nan")


def estimate_true_peak_db(audio: np.ndarray, sample_peak_db: float) -> float:
    if audio.shape[0] < 2:
        return sample_peak_db
    max_abs = float(np.max(np.abs(audio)))
    slope = float(np.max(np.abs(np.diff(audio, axis=0))))
    # Lightweight intersample-risk estimate. This is intentionally conservative
    # enough for trend analysis without decoding every file through ffmpeg.
    estimated = min(1.75, max_abs + (slope * 0.18))
    return db_from_linear(estimated)


def window_lufs_estimates(power: np.ndarray, sample_rate: int, integrated_lufs: float, rms_db: float) -> dict[str, float]:
    offset = rms_db - integrated_lufs if math.isfinite(integrated_lufs) else 0.0

    def stats(window_seconds: float, hop_seconds: float) -> tuple[float, float]:
        window = max(1, int(round(window_seconds * sample_rate)))
        hop = max(1, int(round(hop_seconds * sample_rate)))
        if len(power) < window:
            value = db_from_power(float(np.mean(power))) - offset
            return value, value

        cumsum = np.concatenate(([0.0], np.cumsum(power, dtype=np.float64)))
        values: list[float] = []
        for start in range(0, len(power) - window + 1, hop):
            p = float((cumsum[start + window] - cumsum[start]) / window)
            values.append(db_from_power(p) - offset)
        finite = [v for v in values if math.isfinite(v) and v > -120.0]
        if not finite:
            return -120.0, -120.0
        return max(finite), mean(finite)

    momentary_max, _ = stats(0.400, 0.200)
    short_max, short_avg = stats(3.000, 1.000)
    return {
        "momentary_lufs_max": momentary_max,
        "short_term_lufs_max": short_max,
        "short_term_lufs_avg": short_avg,
    }


def spectral_metrics(mono: np.ndarray, sample_rate: int) -> dict[str, float]:
    n_fft = 65536
    if len(mono) < 2048:
        return {name: float("nan") for name, _, _ in BANDS}

    window = np.hanning(n_fft).astype(np.float32)
    power_sum: np.ndarray | None = None
    block_count = 0
    for start in range(0, len(mono), n_fft):
        block = mono[start : start + n_fft]
        if len(block) < 2048:
            continue
        padded = np.zeros(n_fft, dtype=np.float32)
        padded[: len(block)] = block
        spectrum = np.fft.rfft(padded * window)
        power = (np.abs(spectrum) ** 2).astype(np.float64)
        if power_sum is None:
            power_sum = power
        else:
            power_sum += power
        block_count += 1

    if power_sum is None or block_count == 0:
        return {name: float("nan") for name, _, _ in BANDS}

    freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
    total = float(np.sum(power_sum[(freqs >= 20.0) & (freqs <= min(sample_rate * 0.5, 24000.0))]))
    total = max(total, 1.0e-24)

    out: dict[str, float] = {}
    for name, low, high in BANDS:
        upper = min(high, sample_rate * 0.5)
        mask = (freqs >= low) & (freqs < upper)
        out[f"band_{name}_db_rel"] = db_from_power(float(np.sum(power_sum[mask])) / total)

    audible = (freqs >= 20.0) & (freqs <= min(sample_rate * 0.5, 24000.0))
    audible_power = power_sum[audible]
    audible_freqs = freqs[audible]
    audible_total = max(float(np.sum(audible_power)), 1.0e-24)
    out["spectral_centroid_hz"] = float(np.sum(audible_freqs * audible_power) / audible_total)
    out["spectral_flatness"] = float(np.exp(np.mean(np.log(audible_power + 1.0e-18))) / np.mean(audible_power + 1.0e-18))
    harsh = float(np.sum(power_sum[(freqs >= 2500.0) & (freqs <= min(sample_rate * 0.5, 6500.0))]))
    high = float(np.sum(power_sum[(freqs >= 6500.0) & (freqs <= min(sample_rate * 0.5, 14000.0))]))
    out["high_band_harshness_index"] = (harsh + high * 0.35) / audible_total
    cumulative = np.cumsum(audible_power)
    rolloff_index = int(np.searchsorted(cumulative, audible_total * 0.95))
    rolloff_index = min(max(rolloff_index, 0), len(audible_freqs) - 1)
    out["high_frequency_rolloff_hz"] = float(audible_freqs[rolloff_index])
    return out


def low_end_mono_metrics(audio: np.ndarray, sample_rate: int) -> dict[str, float]:
    if audio.shape[1] < 2 or len(audio) < 2048:
        return {"low_end_mono_delta_db": 0.0, "low_band_correlation": 1.0, "erb_bass_mono_risk": 0.0}

    n_fft = 8192
    window = np.hanning(n_fft).astype(np.float32)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
    low = (freqs >= 20.0) & (freqs <= min(sample_rate * 0.5, 160.0))
    stereo_energy = 0.0
    mono_energy = 0.0
    low_left_energy = 0.0
    low_right_energy = 0.0
    low_cross_energy = 0.0
    for start in range(0, len(audio), n_fft):
        block = audio[start : start + n_fft]
        if len(block) < 2048:
            continue
        padded = np.zeros((n_fft, audio.shape[1]), dtype=np.float32)
        padded[: len(block), :] = block
        left = padded[:, 0] * window
        right = padded[:, 1] * window
        mono = ((padded[:, 0] + padded[:, 1]) * 0.5) * window
        left_fft = np.fft.rfft(left)
        right_fft = np.fft.rfft(right)
        mono_fft = np.fft.rfft(mono)
        left_power = np.abs(left_fft) ** 2
        right_power = np.abs(right_fft) ** 2
        mono_power = np.abs(mono_fft) ** 2
        low_left = float(np.sum(left_power[low]))
        low_right = float(np.sum(right_power[low]))
        stereo_energy += (low_left + low_right) * 0.5
        mono_energy += float(np.sum(mono_power[low]))
        low_left_energy += low_left
        low_right_energy += low_right
        low_cross_energy += float(np.real(np.sum(left_fft[low] * np.conj(right_fft[low]))))

    low_end_mono_delta = db_from_power(mono_energy) - db_from_power(stereo_energy)
    low_band_correlation = low_cross_energy / math.sqrt(max(low_left_energy * low_right_energy, 1.0e-18))
    low_band_correlation = max(-1.0, min(1.0, low_band_correlation))
    mono_loss = max(0.0, -low_end_mono_delta)
    return {
        "low_end_mono_delta_db": low_end_mono_delta,
        "low_band_correlation": low_band_correlation,
        "erb_bass_mono_risk": max(0.0, min(1.0, ((1.0 - low_band_correlation) * 0.5) + (mono_loss / 9.0))),
    }


def transient_metrics(mono: np.ndarray, sample_rate: int) -> dict[str, float]:
    frame = max(1, int(round(0.010 * sample_rate)))
    if len(mono) < frame:
        return {"transient_density_per_sec": 0.0, "peak_density_per_sec": 0.0}

    transient_count = 0
    peak_count = 0
    frames = 0
    for start in range(0, len(mono) - frame + 1, frame):
        x = mono[start : start + frame]
        peak_db = db_from_linear(float(np.max(np.abs(x))))
        rms_db = db_from_linear(float(np.sqrt(np.mean(x * x))))
        if peak_db > -18.0 and (peak_db - rms_db) > 8.0:
            transient_count += 1
        if peak_db > -1.0:
            peak_count += 1
        frames += 1
    duration = len(mono) / sample_rate
    return {
        "transient_density_per_sec": transient_count / max(duration, 1.0e-9),
        "peak_density_per_sec": peak_count / max(duration, 1.0e-9),
    }


def stereo_metrics(audio: np.ndarray) -> dict[str, float]:
    if audio.shape[1] < 2:
        return {"stereo_correlation": 1.0, "mono_downmix_delta_db": 0.0}
    left = audio[:, 0].astype(np.float64)
    right = audio[:, 1].astype(np.float64)
    left -= np.mean(left)
    right -= np.mean(right)
    denom = math.sqrt(float(np.sum(left * left) * np.sum(right * right)))
    corr = float(np.sum(left * right) / denom) if denom > 0.0 else 1.0
    stereo_rms = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))
    mono = np.mean(audio, axis=1)
    mono_rms = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
    return {
        "stereo_correlation": max(-1.0, min(1.0, corr)),
        "mono_downmix_delta_db": db_from_linear(mono_rms) - db_from_linear(stereo_rms),
    }


def type_candidate(row: dict[str, Any]) -> str:
    lufs = row.get("integrated_lufs", float("nan"))
    crest = row.get("crest_factor_db", float("nan"))
    low = row.get("band_20_80_hz_db_rel", -120.0) + row.get("band_80_160_hz_db_rel", -120.0)
    high = row.get("band_8k_16k_hz_db_rel", -120.0) + row.get("band_16k_plus_hz_db_rel", -120.0)
    transients = row.get("transient_density_per_sec", 0.0)
    if high > -32.0:
        return "Bright"
    if low > -23.0:
        return "Bass"
    if transients > 10.0:
        return "Drums"
    if math.isfinite(lufs) and lufs < -7.0 and math.isfinite(crest) and crest < 8.0:
        return "EDM"
    return "Glue"


def analyze_file(path: Path, album: Path) -> dict[str, Any]:
    info = sf.info(str(path))
    audio, sample_rate = sf.read(str(path), dtype="float32", always_2d=True)
    audio = np.nan_to_num(audio, copy=False)
    channels = audio.shape[1]
    duration = audio.shape[0] / float(sample_rate)
    mono = np.mean(audio, axis=1).astype(np.float32)
    power = np.mean(audio.astype(np.float64) ** 2, axis=1)

    loudnorm = run_loudnorm(path) if USE_FFMPEG_LOUDNORM else {}
    sample_peak_db = db_from_linear(float(np.max(np.abs(audio))))
    rms_db = db_from_linear(float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))))
    integrated_lufs = loudnorm.get("integrated_lufs", float("nan"))
    if not math.isfinite(integrated_lufs):
        integrated_lufs = fallback_integrated_lufs(audio, sample_rate)
    true_peak_dbtp = loudnorm.get("true_peak_dbtp", estimate_true_peak_db(audio, sample_peak_db))
    lufs_windows = window_lufs_estimates(power, sample_rate, integrated_lufs, rms_db)
    crest = sample_peak_db - rms_db

    row: dict[str, Any] = {
        "album": album.name,
        "file": path.name,
        "path": str(path),
        "duration_sec": duration,
        "sample_rate": sample_rate,
        "channels": channels,
        "integrated_lufs": integrated_lufs,
        "short_term_lufs_max": lufs_windows["short_term_lufs_max"],
        "short_term_lufs_avg": lufs_windows["short_term_lufs_avg"],
        "momentary_lufs_max": lufs_windows["momentary_lufs_max"],
        "true_peak_dbtp": true_peak_dbtp,
        "sample_peak_dbfs": sample_peak_db,
        "rms_db": rms_db,
        "crest_factor_db": crest,
        "plr_db": true_peak_dbtp - integrated_lufs if math.isfinite(integrated_lufs) else float("nan"),
        "psr_db": true_peak_dbtp - lufs_windows["short_term_lufs_max"],
        "dr_like_score": crest,
        "near_clip_sample_count": int(np.count_nonzero(np.abs(audio) >= 0.999)),
    }
    row.update(stereo_metrics(audio))
    row.update(spectral_metrics(mono, sample_rate))
    row.update(transient_metrics(mono, sample_rate))
    row.update(low_end_mono_metrics(audio, sample_rate))
    near_clip_ratio = row["near_clip_sample_count"] / max(1, audio.size)
    row["near_clip_ratio"] = near_clip_ratio
    row["peak_budget_pressure_proxy"] = max(
        0.0,
        min(1.0, ((true_peak_dbtp + 1.0) / 8.0) + (near_clip_ratio * 120.0) + max(0.0, 6.0 - crest) / 18.0),
    )
    row["sustain_density_proxy"] = max(
        0.0,
        min(
            1.0,
            (1.0 - row.get("spectral_flatness", 0.0) * 0.55)
            * (1.0 - min(1.0, row.get("transient_density_per_sec", 0.0) / 24.0) * 0.35)
            * max(0.0, 1.0 - max(0.0, crest - 10.0) / 18.0),
        ),
    )
    row["transient_budget_risk"] = max(
        0.0,
        min(1.0, (row.get("transient_density_per_sec", 0.0) / 28.0) * (0.35 + row["peak_budget_pressure_proxy"] * 0.65)),
    )
    row["peakeater_type_candidate"] = type_candidate(row)
    return row


def finite_values(rows: list[dict[str, Any]], key: str) -> list[float]:
    values = [clean_float(r.get(key)) for r in rows]
    return [v for v in values if math.isfinite(v)]


def fmt(value: Any, digits: int = 2) -> str:
    value = clean_float(value)
    if not math.isfinite(value):
        return "n/a"
    return f"{value:.{digits}f}"


def html_table(rows: list[dict[str, Any]], columns: list[tuple[str, str]], limit: int | None = None) -> str:
    body_rows = rows[:limit] if limit is not None else rows
    header = "".join(f"<th>{html.escape(label)}</th>" for key, label in columns)
    body = []
    for row in body_rows:
        cells = []
        for key, _label in columns:
            value = row.get(key, "")
            if isinstance(value, float):
                text = fmt(value)
            else:
                text = str(value)
            cells.append(f"<td>{html.escape(text)}</td>")
        body.append("<tr>" + "".join(cells) + "</tr>")
    return f"<table><thead><tr>{header}</tr></thead><tbody>{''.join(body)}</tbody></table>"


def histogram_svg(values: list[float], title: str, unit: str, width: int = 720, height: int = 170) -> str:
    finite = [v for v in values if math.isfinite(v)]
    if not finite:
        return f"<p>No data for {html.escape(title)}.</p>"
    bins = min(14, max(5, int(math.sqrt(len(finite)))))
    counts, edges = np.histogram(np.array(finite), bins=bins)
    max_count = max(int(np.max(counts)), 1)
    pad = 34
    chart_w = width - pad * 2
    chart_h = height - pad * 2
    bars = []
    for i, count in enumerate(counts):
        x = pad + (chart_w * i / bins)
        bar_w = chart_w / bins - 3
        bar_h = chart_h * (count / max_count)
        y = pad + chart_h - bar_h
        bars.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{bar_h:.1f}" rx="2" />')
    label_min = f"{edges[0]:.1f}{unit}"
    label_max = f"{edges[-1]:.1f}{unit}"
    return (
        f'<svg class="chart" viewBox="0 0 {width} {height}" role="img">'
        f'<text x="{pad}" y="20">{html.escape(title)}</text>'
        f'<g class="bars">{"".join(bars)}</g>'
        f'<text x="{pad}" y="{height - 8}">{label_min}</text>'
        f'<text x="{width - pad - 80}" y="{height - 8}">{label_max}</text>'
        "</svg>"
    )


def album_summaries(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    summaries: list[dict[str, Any]] = []
    albums = sorted({r["album"] for r in rows})
    for album in albums:
        album_rows = [r for r in rows if r["album"] == album]
        if not album_rows:
            continue
        loudest = min(album_rows, key=lambda r: clean_float(r.get("integrated_lufs"), 999.0))
        true_peak = max(album_rows, key=lambda r: clean_float(r.get("true_peak_dbtp"), -999.0))
        high_track = max(album_rows, key=lambda r: clean_float(r.get("band_8k_16k_hz_db_rel"), -999.0))
        low_track = max(album_rows, key=lambda r: clean_float(r.get("band_20_80_hz_db_rel"), -999.0))
        summaries.append(
            {
                "album": album,
                "tracks": len(album_rows),
                "total_minutes": sum(r["duration_sec"] for r in album_rows) / 60.0,
                "avg_lufs": mean(finite_values(album_rows, "integrated_lufs")),
                "avg_true_peak": mean(finite_values(album_rows, "true_peak_dbtp")),
                "avg_crest": mean(finite_values(album_rows, "crest_factor_db")),
                "avg_high_8k_16k": mean(finite_values(album_rows, "band_8k_16k_hz_db_rel")),
                "avg_low_20_80": mean(finite_values(album_rows, "band_20_80_hz_db_rel")),
                "avg_stereo_corr": mean(finite_values(album_rows, "stereo_correlation")),
                "avg_bass_mono_risk": mean(finite_values(album_rows, "erb_bass_mono_risk")),
                "avg_peak_budget_pressure": mean(finite_values(album_rows, "peak_budget_pressure_proxy")),
                "avg_sustain_density": mean(finite_values(album_rows, "sustain_density_proxy")),
                "loudest_track": loudest["file"],
                "highest_true_peak_track": true_peak["file"],
                "brightest_track": high_track["file"],
                "lowest_track": low_track["file"],
                "type_candidates": ", ".join(sorted({r["peakeater_type_candidate"] for r in album_rows})),
            }
        )
    return summaries


def recommendations(rows: list[dict[str, Any]]) -> list[str]:
    lufs_values = finite_values(rows, "integrated_lufs")
    tp_values = finite_values(rows, "true_peak_dbtp")
    crest_values = finite_values(rows, "crest_factor_db")
    hf_values = finite_values(rows, "band_8k_16k_hz_db_rel")
    bass_risk_values = finite_values(rows, "erb_bass_mono_risk")
    peak_budget_values = finite_values(rows, "peak_budget_pressure_proxy")
    recs = [
        "Do not spend extra Quality CPU on stronger full-band spectral suppression. These references are already dense; broad suppression reads as darker, not louder.",
        "Use higher Quality mainly for true-peak detector precision, final ceiling smoothing, and nonlinear interpolation around soft/final clipping.",
        "Keep EDM/Drums/Bass final clipping available. Removing the clipper too aggressively reduces perceived loudness and pushes the limiter into dull broadband gain reduction.",
    ]
    if lufs_values and median(lufs_values) < -8.0:
        recs.append("Median integrated loudness is already competitive. Additional loudness should come from controlled density and transient-aware clipping, not static gain reduction.")
    if tp_values and max(tp_values) > -0.3:
        recs.append("Several tracks sit close to or above true-peak danger. Peakeater Spectral should improve detector-path true peak handling before raising safety margins.")
    if crest_values and median(crest_values) < 8.0:
        recs.append("Crest factor is low, so Punch Protect and transient recovery should remain active when Drive is pushed.")
    if hf_values and median(hf_values) > -18.0:
        recs.append("High-frequency energy is part of the genre signature. HF Guard should act dynamically on harsh bursts only, not as a static top-end shelf.")
    if bass_risk_values and max(bass_risk_values) > 0.45:
        recs.append("Some tracks show low-end mono risk. Bass Lock should react to low-band correlation, not just full-band stereo correlation.")
    if peak_budget_values and median(peak_budget_values) > 0.45:
        recs.append("Peak budget is tight on these references. Clean loudness should come from sustain-only upward density and micro-GR, not more broadband Drive.")
    return recs


def write_outputs(rows: list[dict[str, Any]], summaries: list[dict[str, Any]]) -> None:
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys()) if rows else []
    with CSV_PATH.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    with JSON_PATH.open("w", encoding="utf-8") as f:
        json.dump({"tracks": rows, "albums": summaries}, f, ensure_ascii=False, indent=2)

    lufs = finite_values(rows, "integrated_lufs")
    true_peak = finite_values(rows, "true_peak_dbtp")
    crest = finite_values(rows, "crest_factor_db")
    recs = recommendations(rows)

    album_columns = [
        ("album", "Album"),
        ("tracks", "Tracks"),
        ("total_minutes", "Minutes"),
        ("avg_lufs", "Avg LUFS-I"),
        ("avg_true_peak", "Avg dBTP"),
        ("avg_crest", "Avg Crest"),
        ("avg_high_8k_16k", "Avg 8-16k rel"),
        ("avg_low_20_80", "Avg 20-80 rel"),
        ("avg_stereo_corr", "Stereo Corr"),
        ("avg_bass_mono_risk", "Bass Mono Risk"),
        ("avg_peak_budget_pressure", "Peak Budget"),
        ("avg_sustain_density", "Sustain Proxy"),
        ("type_candidates", "Type candidates"),
    ]
    track_columns = [
        ("album", "Album"),
        ("file", "Track"),
        ("integrated_lufs", "LUFS-I"),
        ("short_term_lufs_max", "LUFS-S Max"),
        ("momentary_lufs_max", "LUFS-M Max"),
        ("true_peak_dbtp", "dBTP"),
        ("sample_peak_dbfs", "Peak"),
        ("rms_db", "RMS"),
        ("crest_factor_db", "Crest"),
        ("plr_db", "PLR"),
        ("psr_db", "PSR"),
        ("near_clip_sample_count", "Near Clip Samples"),
        ("peak_budget_pressure_proxy", "Peak Budget"),
        ("sustain_density_proxy", "Sustain Proxy"),
        ("transient_budget_risk", "Transient Risk"),
        ("stereo_correlation", "Stereo Corr"),
        ("mono_downmix_delta_db", "Mono Delta"),
        ("low_end_mono_delta_db", "Low Mono Delta"),
        ("low_band_correlation", "Low Corr"),
        ("erb_bass_mono_risk", "Bass Mono Risk"),
        ("band_20_80_hz_db_rel", "20-80"),
        ("band_80_160_hz_db_rel", "80-160"),
        ("band_4k_8k_hz_db_rel", "4-8k"),
        ("band_8k_16k_hz_db_rel", "8-16k"),
        ("spectral_centroid_hz", "Centroid"),
        ("spectral_flatness", "Flatness"),
        ("high_band_harshness_index", "Harsh"),
        ("high_frequency_rolloff_hz", "Rolloff"),
        ("transient_density_per_sec", "Transient/s"),
        ("peakeater_type_candidate", "Type"),
    ]

    summary_text = [
        f"Tracks analyzed: {len(rows)}",
        f"Median LUFS-I: {fmt(median(lufs)) if lufs else 'n/a'}",
        f"Max true peak: {fmt(max(true_peak)) if true_peak else 'n/a'} dBTP",
        f"Median crest factor: {fmt(median(crest)) if crest else 'n/a'} dB",
    ]

    css = """
    body { font-family: Segoe UI, Arial, sans-serif; margin: 24px; background: #101314; color: #e8ecec; }
    h1, h2, h3 { color: #ffffff; }
    section { margin: 28px 0; }
    .note, .card { background: #1a2022; border: 1px solid #35505a; border-radius: 8px; padding: 14px 16px; margin: 12px 0; }
    table { border-collapse: collapse; width: 100%; font-size: 12px; margin: 12px 0; }
    th, td { border: 1px solid #30393c; padding: 6px 7px; vertical-align: top; }
    th { background: #223039; color: #ffffff; position: sticky; top: 0; }
    tr:nth-child(even) { background: #151a1c; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; }
    .chart { width: 100%; height: auto; background: #151a1c; border: 1px solid #30393c; border-radius: 8px; }
    .chart text { fill: #d8eeee; font-size: 13px; }
    .bars rect { fill: #37c7b5; opacity: 0.86; }
    code { color: #9ee8df; }
    """
    html_doc = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Peakeater Spectral Reference Analysis</title>
<style>{css}</style>
</head>
<body>
<h1>Peakeater Spectral Reference Analysis</h1>
<p class="note">Generated from mastered Drum'n Bass / Dubstep FLAC references. Audio was analyzed locally; no audio is embedded in this report. True Peak is a lightweight intersample-risk estimate unless <code>USE_FFMPEG_LOUDNORM</code> is enabled in the script.</p>

<section>
<h2>1. Executive Summary</h2>
<div class="grid">
{''.join(f'<div class="card">{html.escape(item)}</div>' for item in summary_text)}
</div>
<ul>{''.join(f'<li>{html.escape(item)}</li>' for item in recs)}</ul>
</section>

<section>
<h2>2. Dataset Overview</h2>
{html_table(summaries, album_columns)}
</section>

<section>
<h2>3. Loudness / True Peak Analysis</h2>
<div class="grid">
{histogram_svg(lufs, 'Integrated LUFS Distribution', ' LUFS')}
{histogram_svg(true_peak, 'True Peak Distribution', ' dBTP')}
{histogram_svg(crest, 'Crest Factor Distribution', ' dB')}
</div>
</section>

<section>
<h2>4. Spectral Balance Analysis</h2>
<p>The relative band values are dB share of full-spectrum energy. Less negative means that band is more dominant in the master.</p>
<p>For Peakeater Spectral, the important reading is not only how much high-frequency energy exists, but whether Quality increases are causing this region to be statically suppressed. If this report shows strong 8-16k presence, HF Guard must remain event-driven.</p>
</section>

<section>
<h2>5. Clip / Limiter Risk Analysis</h2>
<p>Near-clip samples and dBTP values identify tracks that are already at mastering limits. These should guide Drive safety and true-peak detector tuning.</p>
</section>

<section>
<h2>6. Stereo / Mono Safety</h2>
<p>Stereo correlation and mono downmix delta are included to help tune Bass Lock / Low Protect. Low-end protection should not darken the mid/high range.</p>
</section>

<section>
<h2>7. Peakeater Spectral Improvement Notes</h2>
<ul>
<li><b>Drive:</b> Keep Drive as the loudness generator. Safety should be dynamic and peak-triggered, not a broadband loudness reduction.</li>
<li><b>Final Clip:</b> Preserve a controlled clipper path for EDM, Drums, Bass and HipHop. Removing it forces dull limiter gain reduction.</li>
<li><b>True Peak:</b> Use CPU for detector-path true peak and final ceiling accuracy at High and above.</li>
<li><b>Spectral Bank:</b> Keep spectral control shallow by default. Increase precision, not constant suppression.</li>
<li><b>HF Guard:</b> Trigger on harsh bursts and high transient density. Avoid static high shelf reduction.</li>
<li><b>Low Protect:</b> Protect sub/kick detector behavior without reducing mid/high loudness.</li>
<li><b>Quality:</b> Eco/Live should remain loud and responsive. Master/Ultra/Max should improve safety and transparency.</li>
</ul>
</section>

<section>
<h2>8. Recommended DSP Changes</h2>
<ol>
<li>Keep <code>maxRealtimeBandMultiplier</code> conservative; do not raise all spectral bands as the default High+ strategy.</li>
<li>Move High+ CPU budget toward true-peak prediction, final ceiling smoothing, and nonlinear interpolation.</li>
<li>Limit HF Guard to short high-frequency risk events. Avoid permanent high-frequency target lowering.</li>
<li>Keep Final Clip partially available on loud genre modes, with oversampled/interpolated quality rather than removal.</li>
<li>Use Low Protect as detector weighting, not a broad low-preserving / high-dulling macro.</li>
</ol>
</section>

<section>
<h2>9. Track Detail Table</h2>
{html_table(rows, track_columns)}
</section>
</body>
</html>"""

    HTML_PATH.write_text(html_doc, encoding="utf-8")


def main() -> int:
    configure_console()
    albums = find_target_albums()
    files: list[tuple[Path, Path]] = []
    for album in albums:
        album_files = sorted(album.rglob("*.flac"), key=lambda p: str(p).lower())
        print(f"{album.name}: {len(album_files)} FLAC", flush=True)
        files.extend((f, album) for f in album_files)

    if not files:
        raise RuntimeError("No FLAC files found in target albums.")

    rows: list[dict[str, Any]] = []
    failures: list[dict[str, str]] = []
    for index, (path, album) in enumerate(files, start=1):
        print(f"[{index}/{len(files)}] {album.name} / {path.name}", flush=True)
        try:
            rows.append(analyze_file(path, album))
        except Exception as exc:
            failures.append({"path": str(path), "error": str(exc)})
            print(f"ERROR: {path}: {exc}", flush=True)

    if not rows:
        raise RuntimeError("All files failed analysis.")

    summaries = album_summaries(rows)
    if failures:
        DOCS_DIR.mkdir(parents=True, exist_ok=True)
        (DOCS_DIR / "peakeater_spectral_reference_analysis_failures.json").write_text(
            json.dumps(failures, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    write_outputs(rows, summaries)
    size_mb = HTML_PATH.stat().st_size / (1024 * 1024)
    print(f"HTML: {HTML_PATH} ({size_mb:.2f} MB)", flush=True)
    print(f"CSV:  {CSV_PATH}", flush=True)
    print(f"JSON: {JSON_PATH}", flush=True)
    print(f"Analyzed tracks: {len(rows)}; failures: {len(failures)}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
