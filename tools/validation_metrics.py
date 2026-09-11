#!/usr/bin/env python3
"""Offline audio metrics for Peakeater Spectral validation.

This intentionally keeps dependencies optional. If soundfile is installed then
FLAC/WAV/AIFF can be read. Otherwise Python's wave module is used for WAV.
"""

from __future__ import annotations

import argparse
import csv
import math
import pathlib
import wave

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover
    raise SystemExit("numpy is required for validation_metrics.py") from exc

try:
    import soundfile as sf
except ImportError:  # pragma: no cover
    sf = None

try:
    from scipy import signal
except ImportError:  # pragma: no cover
    signal = None


def db(value: float) -> float:
    return 20.0 * math.log10(max(float(value), 1.0e-12))


def lufs_from_ms(value: float) -> float:
    if value <= 0.0:
        return -120.0
    return max(-120.0, min(24.0, -0.691 + (10.0 * math.log10(value))))


def lufs_array(values: np.ndarray) -> np.ndarray:
    return np.clip(-0.691 + (10.0 * np.log10(np.maximum(values, 1.0e-12))), -120.0, 24.0)


def make_high_pass(sample_rate: int) -> tuple[float, float, float, float, float]:
    k = math.tan(math.pi * 38.0 / sample_rate)
    q = 0.5
    norm = 1.0 / (1.0 + (k / q) + (k * k))
    return (
        norm,
        -2.0 * norm,
        norm,
        2.0 * (k * k - 1.0) * norm,
        (1.0 - (k / q) + (k * k)) * norm,
    )


def make_high_shelf(sample_rate: int) -> tuple[float, float, float, float, float]:
    gain = 10.0 ** (4.0 / 40.0)
    w0 = 2.0 * math.pi * 1681.974450955533 / sample_rate
    alpha = math.sin(w0) / (2.0 * 0.7071752369554196)
    cos_w0 = math.cos(w0)
    sqrt_a = math.sqrt(gain)
    b0 = gain * ((gain + 1.0) + ((gain - 1.0) * cos_w0) + (2.0 * sqrt_a * alpha))
    b1 = -2.0 * gain * ((gain - 1.0) + ((gain + 1.0) * cos_w0))
    b2 = gain * ((gain + 1.0) + ((gain - 1.0) * cos_w0) - (2.0 * sqrt_a * alpha))
    a0 = (gain + 1.0) - ((gain - 1.0) * cos_w0) + (2.0 * sqrt_a * alpha)
    a1 = 2.0 * ((gain - 1.0) - ((gain + 1.0) * cos_w0))
    a2 = (gain + 1.0) - ((gain - 1.0) * cos_w0) - (2.0 * sqrt_a * alpha)
    return b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0


def apply_biquad(data: np.ndarray, coeffs: tuple[float, float, float, float, float]) -> np.ndarray:
    b0, b1, b2, a1, a2 = coeffs
    if signal is not None:
        return signal.lfilter([b0, b1, b2], [1.0, a1, a2], data, axis=0).astype(np.float64)

    output = np.zeros_like(data, dtype=np.float64)
    x1 = np.zeros(data.shape[1], dtype=np.float64)
    x2 = np.zeros(data.shape[1], dtype=np.float64)
    y1 = np.zeros(data.shape[1], dtype=np.float64)
    y2 = np.zeros(data.shape[1], dtype=np.float64)
    for i in range(data.shape[0]):
        x0 = data[i].astype(np.float64)
        y0 = (b0 * x0) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2)
        output[i] = y0
        x2 = x1
        x1 = x0
        y2 = y1
        y1 = y0
    return output


def k_weighted(data: np.ndarray, sample_rate: int) -> np.ndarray:
    high_passed = apply_biquad(data, make_high_pass(sample_rate))
    return apply_biquad(high_passed, make_high_shelf(sample_rate))


def channel_power(weighted: np.ndarray) -> np.ndarray:
    if weighted.size == 0:
        return np.zeros(0, dtype=np.float64)
    return np.sum(np.square(weighted), axis=1)


def window_lufs(power: np.ndarray, sample_rate: int, seconds: float) -> tuple[float, float]:
    window = max(1, int(round(seconds * sample_rate)))
    if power.size < window:
        value = lufs_from_ms(float(np.mean(power))) if power.size else -120.0
        return value, value
    kernel = np.ones(window, dtype=np.float64) / window
    mean = np.convolve(power, kernel, mode="valid")
    values = lufs_array(mean)
    return float(np.max(values)), float(np.mean(values))


def integrated_lufs(power: np.ndarray, sample_rate: int) -> float:
    block = max(1, int(round(0.4 * sample_rate)))
    if power.size < block:
        return lufs_from_ms(float(np.mean(power))) if power.size else -120.0
    block_count = power.size // block
    blocks = power[: block_count * block].reshape(block_count, block).mean(axis=1)
    ungated = blocks[lufs_array(blocks) > -70.0]
    if ungated.size == 0:
        return -120.0
    ungated_mean = float(np.mean(ungated))
    relative_gate = lufs_from_ms(ungated_mean) - 10.0
    gated = ungated[lufs_array(ungated) > relative_gate]
    return lufs_from_ms(float(np.mean(gated if gated.size else ungated)))


def spectral_descriptors(data: np.ndarray, sample_rate: int) -> dict[str, float]:
    if data.size == 0:
        return {
            "spectral_centroid_hz": 0.0,
            "spectral_flatness": 0.0,
            "onset_density_per_sec": 0.0,
            "high_band_harshness_index": 0.0,
            "low_end_mono_delta_db": 0.0,
            "low_band_correlation": 1.0,
            "erb_bass_mono_risk": 0.0,
            "dynamic_complexity_db": 0.0,
        }

    frame_size = min(4096, max(512, int(round(sample_rate * 0.046))))
    hop_size = max(128, frame_size // 2)
    if data.shape[0] < frame_size:
        pad = frame_size - data.shape[0]
        data = np.pad(data, ((0, pad), (0, 0)), mode="constant")

    mono = np.mean(data, axis=1)
    left = data[:, 0]
    right = data[:, 1] if data.shape[1] > 1 else data[:, 0]
    window = np.hanning(frame_size).astype(np.float64)
    freqs = np.fft.rfftfreq(frame_size, 1.0 / sample_rate)
    audible_mask = (freqs >= 20.0) & (freqs <= 20000.0)
    harsh_mask = (freqs >= 2500.0) & (freqs <= 6500.0)
    high_mask = (freqs >= 6500.0) & (freqs <= 14000.0)
    low_mask = (freqs >= 20.0) & (freqs <= 160.0)

    centroids: list[float] = []
    flatness_values: list[float] = []
    harsh_ratios: list[float] = []
    rms_values: list[float] = []
    flux_values: list[float] = []
    low_stereo_energy = 0.0
    low_mono_energy = 0.0
    low_left_energy = 0.0
    low_right_energy = 0.0
    low_cross_energy = 0.0
    previous_mag: np.ndarray | None = None

    for start in range(0, data.shape[0] - frame_size + 1, hop_size):
        mono_frame = mono[start : start + frame_size].astype(np.float64) * window
        mag = np.abs(np.fft.rfft(mono_frame))
        power = mag * mag
        audible_power = power[audible_mask]
        audible_freqs = freqs[audible_mask]
        total_power = float(np.sum(audible_power))
        if total_power > 1.0e-16:
            centroids.append(float(np.sum(audible_freqs * audible_power) / total_power))
            flatness_values.append(float(np.exp(np.mean(np.log(audible_power + 1.0e-18))) / np.mean(audible_power + 1.0e-18)))
            harsh = float(np.sum(power[harsh_mask]))
            high = float(np.sum(power[high_mask]))
            harsh_ratios.append((harsh + high * 0.35) / total_power)

        rms_values.append(float(np.sqrt(np.mean(mono_frame * mono_frame))))
        if previous_mag is not None:
            flux_values.append(float(np.sum(np.maximum(0.0, mag - previous_mag)) / max(float(np.sum(mag)), 1.0e-12)))
        previous_mag = mag

        left_fft = np.fft.rfft(left[start : start + frame_size].astype(np.float64) * window)
        right_fft = np.fft.rfft(right[start : start + frame_size].astype(np.float64) * window)
        mono_fft = np.fft.rfft(((left[start : start + frame_size] + right[start : start + frame_size]) * 0.5).astype(np.float64) * window)
        left_power = np.abs(left_fft) ** 2
        right_power = np.abs(right_fft) ** 2
        mono_power = np.abs(mono_fft) ** 2
        low_left = float(np.sum(left_power[low_mask]))
        low_right = float(np.sum(right_power[low_mask]))
        low_stereo_energy += (low_left + low_right) * 0.5
        low_mono_energy += float(np.sum(mono_power[low_mask]))
        low_left_energy += low_left
        low_right_energy += low_right
        low_cross_energy += float(np.real(np.sum(left_fft[low_mask] * np.conj(right_fft[low_mask]))))

    if flux_values:
        flux = np.asarray(flux_values, dtype=np.float64)
        threshold = float(np.median(flux) + (1.5 * np.median(np.abs(flux - np.median(flux)))))
        onset_density = float(np.count_nonzero(flux > threshold) / max(0.001, data.shape[0] / sample_rate))
    else:
        onset_density = 0.0

    rms_array = np.asarray(rms_values, dtype=np.float64)
    dynamic_complexity = float(np.percentile([db(v) for v in rms_array], 95) - np.percentile([db(v) for v in rms_array], 10)) if rms_values else 0.0
    low_end_mono_delta = 10.0 * math.log10(max(low_mono_energy, 1.0e-18) / max(low_stereo_energy, 1.0e-18))
    low_band_correlation = low_cross_energy / math.sqrt(max(low_left_energy * low_right_energy, 1.0e-18))
    low_band_correlation = float(np.clip(low_band_correlation, -1.0, 1.0))
    mono_loss = max(0.0, -low_end_mono_delta)
    erb_bass_mono_risk = float(np.clip((1.0 - low_band_correlation) * 0.5 + (mono_loss / 9.0), 0.0, 1.0))

    return {
        "spectral_centroid_hz": float(np.mean(centroids)) if centroids else 0.0,
        "spectral_flatness": float(np.mean(flatness_values)) if flatness_values else 0.0,
        "onset_density_per_sec": onset_density,
        "high_band_harshness_index": float(np.mean(harsh_ratios)) if harsh_ratios else 0.0,
        "low_end_mono_delta_db": low_end_mono_delta,
        "low_band_correlation": low_band_correlation,
        "erb_bass_mono_risk": erb_bass_mono_risk,
        "dynamic_complexity_db": dynamic_complexity,
    }


def read_audio(path: pathlib.Path) -> tuple[np.ndarray, int]:
    if sf is not None:
        data, sample_rate = sf.read(str(path), always_2d=True, dtype="float32")
        return data, int(sample_rate)

    if path.suffix.lower() != ".wav":
        raise ValueError(f"soundfile is not installed, cannot read {path.suffix}: {path}")

    with wave.open(str(path), "rb") as reader:
        sample_rate = reader.getframerate()
        channels = reader.getnchannels()
        sample_width = reader.getsampwidth()
        frames = reader.readframes(reader.getnframes())

    if sample_width == 2:
        samples = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    elif sample_width == 3:
        raw = np.frombuffer(frames, dtype=np.uint8).reshape(-1, 3)
        signed = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
        signed = np.where(signed & 0x800000, signed | ~0xFFFFFF, signed)
        samples = signed.astype(np.float32) / 8388608.0
    elif sample_width == 4:
        samples = np.frombuffer(frames, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        raise ValueError(f"unsupported WAV sample width {sample_width}: {path}")

    return samples.reshape(-1, channels), sample_rate


def find_reference_file(path: pathlib.Path, reference_root: pathlib.Path | None) -> pathlib.Path | None:
    if reference_root is None:
        return None
    direct = reference_root / path.name
    if direct.exists():
        return direct
    matches = list(reference_root.rglob(path.name))
    return matches[0] if matches else None


def reference_quality_metrics(path: pathlib.Path, reference_root: pathlib.Path | None, data: np.ndarray, sample_rate: int) -> dict[str, object]:
    reference_path = find_reference_file(path, reference_root)
    if reference_path is None:
        return {
            "external_quality_reference_found": False,
            "reference_quality_delta_rms_db": float("nan"),
            "reference_centroid_delta_hz": float("nan"),
            "reference_harshness_delta": float("nan"),
        }

    try:
        reference, reference_sample_rate = read_audio(reference_path)
    except Exception:
        return {
            "external_quality_reference_found": False,
            "reference_quality_delta_rms_db": float("nan"),
            "reference_centroid_delta_hz": float("nan"),
            "reference_harshness_delta": float("nan"),
        }

    if reference_sample_rate != sample_rate:
        return {
            "external_quality_reference_found": False,
            "reference_quality_delta_rms_db": float("nan"),
            "reference_centroid_delta_hz": float("nan"),
            "reference_harshness_delta": float("nan"),
        }

    channels = min(data.shape[1], reference.shape[1])
    samples = min(data.shape[0], reference.shape[0])
    if samples <= 0 or channels <= 0:
        return {
            "external_quality_reference_found": False,
            "reference_quality_delta_rms_db": float("nan"),
            "reference_centroid_delta_hz": float("nan"),
            "reference_harshness_delta": float("nan"),
        }

    current = data[:samples, :channels]
    ref = reference[:samples, :channels]
    error = current - ref
    error_rms = float(np.sqrt(np.mean(np.square(error))))
    ref_rms = float(np.sqrt(np.mean(np.square(ref))))
    current_spectral = spectral_descriptors(current, sample_rate)
    reference_spectral = spectral_descriptors(ref, sample_rate)
    return {
        "external_quality_reference_found": True,
        "reference_quality_delta_rms_db": db(error_rms) - db(ref_rms),
        "reference_centroid_delta_hz": current_spectral["spectral_centroid_hz"] - reference_spectral["spectral_centroid_hz"],
        "reference_harshness_delta": current_spectral["high_band_harshness_index"] - reference_spectral["high_band_harshness_index"],
    }


def estimate_true_peak_4x(data: np.ndarray) -> float:
    peak = float(np.max(np.abs(data))) if data.size else 0.0
    if data.shape[0] < 2:
        return peak

    padded = np.pad(data, ((2, 1), (0, 0)), mode="edge")
    phases = (0.25, 0.5, 0.75)
    for i in range(data.shape[0]):
        p0 = padded[i]
        p1 = padded[i + 1]
        p2 = padded[i + 2]
        p3 = padded[i + 3]
        slope_guard = np.abs(p2 - p1) * 0.018
        curve_guard = np.abs((p2 - p1) - (p1 - p0)) * 0.012
        for t in phases:
            t2 = t * t
            t3 = t2 * t
            interp = 0.5 * ((2.0 * p1) + ((-p0 + p2) * t) + (((2.0 * p0) - (5.0 * p1) + (4.0 * p2) - p3) * t2)
                            + ((-p0 + (3.0 * p1) - (3.0 * p2) + p3) * t3))
            peak = max(peak, float(np.max(np.abs(interp) + slope_guard + curve_guard)))
    return peak


def analyze(path: pathlib.Path, reference_root: pathlib.Path | None = None,
            metadata: dict[str, object] | None = None) -> dict[str, object]:
    data, sample_rate = read_audio(path)
    if data.size == 0:
        raise ValueError(f"empty audio file: {path}")

    data = np.nan_to_num(data, nan=0.0, posinf=0.0, neginf=0.0)
    mono = np.mean(data, axis=1)
    peak = float(np.max(np.abs(data)))
    true_peak = estimate_true_peak_4x(data)
    rms = float(np.sqrt(np.mean(np.square(data))))
    crest = db(peak) - db(rms)
    weighted = k_weighted(data, sample_rate)
    power = channel_power(weighted)
    lufs_m_max, _ = window_lufs(power, sample_rate, 0.4)
    lufs_s_max, lufs_s_avg = window_lufs(power, sample_rate, 3.0)
    lufs_i = integrated_lufs(power, sample_rate)
    spectral = spectral_descriptors(data, sample_rate)
    left = data[:, 0]
    right = data[:, 1] if data.shape[1] > 1 else data[:, 0]
    mid = 0.5 * (left + right)
    side = 0.5 * (left - right)
    mid_peak = float(np.max(np.abs(mid)))
    side_peak = float(np.max(np.abs(side)))
    side_to_mid_ratio_db = db(side_peak) - db(mid_peak)
    denominator = float(np.sqrt(np.sum(left * left) * np.sum(right * right)))
    correlation = float(np.sum(left * right) / denominator) if denominator > 1.0e-12 else 0.0
    mono_peak = float(np.max(np.abs(mono)))
    mono_delta_db = db(mono_peak) - db(peak)
    near_clip = int(np.count_nonzero(np.abs(data) >= 0.999))
    near_clip_ratio = near_clip / max(1, data.size)
    peak_budget_pressure_proxy = float(
        np.clip(((db(true_peak) + 1.0) / 8.0) + (near_clip_ratio * 120.0) + max(0.0, 6.0 - crest) / 18.0, 0.0, 1.0)
    )
    sustain_density_proxy = float(
        np.clip((1.0 - spectral["spectral_flatness"] * 0.55)
                * (1.0 - min(1.0, spectral["onset_density_per_sec"] / 24.0) * 0.35)
                * max(0.0, 1.0 - max(0.0, crest - 10.0) / 18.0), 0.0, 1.0)
    )
    transient_budget_risk = float(
        np.clip((spectral["onset_density_per_sec"] / 28.0) * (0.35 + peak_budget_pressure_proxy * 0.65), 0.0, 1.0)
    )
    loudness_gain_distribution_proxy = float(
        np.clip((max(-36.0, db(rms)) + 36.0) / 30.0
                * (1.0 - min(1.0, near_clip_ratio * 80.0))
                * (1.0 - transient_budget_risk * 0.22), 0.0, 1.0)
    )
    peak_packing_pressure = float(
        np.clip(peak_budget_pressure_proxy * 0.48
                + max(0.0, 9.0 - crest) / 18.0
                + max(0.0, db(true_peak) + 0.5) / 8.0, 0.0, 1.0)
    )
    micro_gr_pressure_proxy = float(
        np.clip(peak_packing_pressure * 0.55
                + transient_budget_risk * 0.18
                + max(0.0, db(true_peak) - db(peak)) / 4.0, 0.0, 1.0)
    )
    sustain_lift_db_proxy = float(
        np.clip((sustain_density_proxy * (1.0 - transient_budget_risk * 0.35)
                 * (1.0 - min(1.0, spectral["erb_bass_mono_risk"] * 0.45))) * 3.0, 0.0, 3.0)
    )

    low_band_energy = float(np.mean(np.square(np.mean(data, axis=1)))) if data.size else 0.0
    if signal is not None and data.shape[0] > 8:
        sos_low = signal.butter(2, 160.0, btype="lowpass", fs=sample_rate, output="sos")
        sos_high = signal.butter(2, 4200.0, btype="highpass", fs=sample_rate, output="sos")
        low_signal = signal.sosfilt(sos_low, data, axis=0)
        high_signal = signal.sosfilt(sos_high, data, axis=0)
        low_band_energy = float(np.mean(np.square(low_signal)))
        high_band_energy = float(np.mean(np.square(high_signal)))
        low_left = low_signal[:, 0]
        low_right = low_signal[:, 1] if low_signal.shape[1] > 1 else low_signal[:, 0]
    else:
        high_band_energy = float(np.mean(np.square(np.diff(data, axis=0)))) if data.shape[0] > 1 else 0.0
        low_left = left
        low_right = right

    low_mid = 0.5 * (low_left + low_right)
    low_side = 0.5 * (low_left - low_right)
    low_mid_energy = float(np.mean(np.square(low_mid)))
    low_side_energy = float(np.mean(np.square(low_side)))
    low_side_to_mid_ratio_db = 10.0 * math.log10(max(low_side_energy, 1.0e-18) / max(low_mid_energy, 1.0e-18))

    clip_pressure = float(np.clip((near_clip_ratio * 180.0) + max(0.0, db(true_peak)) / 6.0, 0.0, 1.0))
    ceiling_pressure = float(np.clip((db(true_peak) + 0.5) / 6.0, 0.0, 1.0))
    transient_proxy = float(np.clip(spectral["onset_density_per_sec"] / 30.0, 0.0, 1.0))
    low_mono_risk = float(spectral["erb_bass_mono_risk"])
    width_preserve_score = float(np.clip(1.0 - low_mono_risk * 0.62 - max(0.0, low_side_to_mid_ratio_db + 18.0) / 48.0, 0.0, 1.0))
    mid_clip_pressure = float(np.clip((db(mid_peak) + 1.0) / 7.0 + near_clip_ratio * 80.0, 0.0, 1.0))
    side_clip_pressure = float(np.clip((db(side_peak) + 1.0) / 7.0 + max(0.0, side_to_mid_ratio_db + 9.0) / 24.0
                                       + near_clip_ratio * 60.0, 0.0, 1.0))

    type_hint = "unknown"
    quality_hint = "unknown"
    lower_name = path.stem.lower()
    for candidate in ("edm", "dubstep", "drumnbass", "dnb", "trap", "808", "kick", "drums", "percs", "bright", "clean", "acoustic"):
        if candidate in lower_name:
            type_hint = "drumnbass" if candidate == "dnb" else candidate
            break
    for candidate in ("eco", "live", "high", "master", "ultra", "max"):
        if candidate in lower_name:
            quality_hint = candidate
            break

    metadata = metadata or {}
    resolved_type_hint = str(metadata.get("type_hint") or type_hint)
    resolved_quality_hint = str(metadata.get("quality_hint") or quality_hint)

    return {
        "file": str(path),
        "plugin_variant": metadata.get("plugin_variant", ""),
        "type_hint": resolved_type_hint,
        "quality_hint": resolved_quality_hint,
        "duration": data.shape[0] / sample_rate,
        "sample_rate": sample_rate,
        "channels": data.shape[1],
        "peak_dbfs": db(peak),
        "true_peak_dbtp_est": db(true_peak),
        "true_peak_est_db": db(true_peak),
        "rms_db": db(rms),
        "lufs_like": lufs_i,
        "lufs_i": lufs_i,
        "lufs_s_max": lufs_s_max,
        "lufs_s_avg": lufs_s_avg,
        "lufs_m_max": lufs_m_max,
        "plr_db": db(true_peak) - lufs_i,
        "psr_db": db(true_peak) - lufs_s_max,
        "crest_db": crest,
        "dr_like": max(0.0, round(crest)),
        "near_clip_samples": near_clip,
        "clip_count": near_clip,
        "near_clip_ratio": near_clip_ratio,
        "gr_max": float("nan"),
        "clip_pressure": clip_pressure,
        "ceiling_pressure": ceiling_pressure,
        "peak_budget_pressure_proxy": peak_budget_pressure_proxy,
        "sustain_density_proxy": sustain_density_proxy,
        "transient_budget_risk": transient_budget_risk,
        "loudness_gain_distribution_proxy": loudness_gain_distribution_proxy,
        "peak_packing_pressure": peak_packing_pressure,
        "micro_gr_pressure_proxy": micro_gr_pressure_proxy,
        "sustain_lift_db_proxy": sustain_lift_db_proxy,
        "transient_proxy": transient_proxy,
        "mid_peak_db": db(mid_peak),
        "side_peak_db": db(side_peak),
        "side_to_mid_ratio_db": side_to_mid_ratio_db,
        "stereo_correlation": correlation,
        "mono_delta_db": mono_delta_db,
        "low_mono_risk": low_mono_risk,
        "low_mid_energy": low_mid_energy,
        "low_side_energy": low_side_energy,
        "low_side_to_mid_ratio_db": low_side_to_mid_ratio_db,
        "width_preserve_score": width_preserve_score,
        "mid_clip_pressure": mid_clip_pressure,
        "side_clip_pressure": side_clip_pressure,
        "low_band_energy": low_band_energy,
        "high_band_energy": high_band_energy,
        "instance_count": metadata.get("instance_count", ""),
        "buffer_size": metadata.get("buffer_size", ""),
        "host_cpu_percent": metadata.get("host_cpu_percent", ""),
        "host_rt_cpu_percent": metadata.get("host_rt_cpu_percent", ""),
        "cpu_notes": metadata.get("cpu_notes", ""),
        **spectral,
        **reference_quality_metrics(path, reference_root, data, sample_rate),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--csv", type=pathlib.Path, default=pathlib.Path("validation_metrics.csv"))
    parser.add_argument("--reference-root", type=pathlib.Path, default=None,
                        help="Optional full-reference folder for processed-vs-reference quality deltas.")
    parser.add_argument("--plugin-variant", default="", help="Optional plugin variant label, e.g. Peakeater Spectral 3.")
    parser.add_argument("--type-hint", default="", help="Override Type label for all input rows.")
    parser.add_argument("--quality-hint", default="", help="Override Quality label for all input rows.")
    parser.add_argument("--instance-count", default="", help="Optional REAPER/Ableton instance count for CPU notes.")
    parser.add_argument("--buffer-size", default="", help="Optional host buffer size for CPU notes.")
    parser.add_argument("--host-cpu-percent", default="", help="Optional host CPU percent measured externally.")
    parser.add_argument("--host-rt-cpu-percent", default="", help="Optional host RT CPU percent measured externally.")
    parser.add_argument("--cpu-notes", default="", help="Optional CPU/RT CPU measurement notes.")
    args = parser.parse_args()

    files: list[pathlib.Path] = []
    for item in args.inputs:
        if item.is_dir():
            files.extend(p for p in item.rglob("*") if p.suffix.lower() in {".wav", ".flac", ".aiff", ".aif"})
        else:
            files.append(item)

    metadata = {
        "plugin_variant": args.plugin_variant,
        "type_hint": args.type_hint,
        "quality_hint": args.quality_hint,
        "instance_count": args.instance_count,
        "buffer_size": args.buffer_size,
        "host_cpu_percent": args.host_cpu_percent,
        "host_rt_cpu_percent": args.host_rt_cpu_percent,
        "cpu_notes": args.cpu_notes,
    }
    rows = [analyze(path, args.reference_root, metadata) for path in files]
    with args.csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()) if rows else ["file"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {len(rows)} rows to {args.csv}")


if __name__ == "__main__":
    main()
