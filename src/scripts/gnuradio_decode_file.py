#!/usr/bin/env python3
"""
GNU Radio file demod helper for RetroSpectrum Decode Workstation.

Input format: interleaved int16 IQ (.complex16/.c16/.iq16)
Output: bitstream to stdout only. Diagnostics go to stderr.

The C workstation launches this script as an external decode engine. The script
imports GNU Radio, uses GNU Radio blocks where they are a good fit, and uses
small NumPy DSP helpers for timing/pulse modes that GNU Radio does not expose as
one universal automatic demodulator block.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from typing import Iterable, Optional

try:
    import numpy as np

except Exception as exc:  # pragma: no cover
    print(f"numpy import failed: {exc}", file=sys.stderr)
    raise SystemExit(2)

try:
    from gnuradio import gr, blocks, digital  # noqa: F401

except Exception as exc:  # pragma: no cover
    print(f"GNU Radio Python import failed: {exc}", file=sys.stderr)
    print("Install GNU Radio, then try again: sudo apt install gnuradio python3-numpy", file=sys.stderr)
    raise SystemExit(2)

MODES = [
    "ook",
    "ook_raw",
    "bpsk",
    "qpsk",
    "psk8",
    "fsk2",
    "gfsk",
    "fsk4",
    "afsk",
    "qam16",
    "qam64",
    "sstv_vis",
]


def read_complex16_raw(path: Path, start_sample: int, count_iq: int) -> np.ndarray:

    # Purpose: Read interleaved int16 IQ samples from disk starting at a requested IQ sample offset

    # Return: A complex64 NumPy array normalized to approximately [-1.0, 1.0] per I/Q component

    if start_sample < 0:
        raise ValueError("start-sample must be >= 0")

    if count_iq <= 0:
        raise ValueError("sample count must be > 0")

    with path.open("rb") as fp:
        fp.seek(start_sample * 4, 0)
        raw = np.fromfile(fp, dtype=np.int16, count=count_iq * 2)

    if raw.size < 2:
        return np.array([], dtype=np.complex64)

    if raw.size % 2:
        raw = raw[:-1]

    iq = raw.reshape((-1, 2)).astype(np.float32)

    return ((iq[:, 0] + 1j * iq[:, 1]) / 32768.0).astype(np.complex64)


def symbol_views(path: Path, start_sample: int, max_symbols: int, sps: int):

    # Purpose: Build symbol-spaced views from raw IQ by grouping samples into fixed samples-per-symbol blocks

    # Return: A tuple of complex symbol averages, magnitude-power symbol averages, and the trimmed raw complex samples

    if sps <= 0:
        raise ValueError("samples/symbol must be > 0")

    if max_symbols <= 0:
        raise ValueError("max-symbols must be > 0")

    c = read_complex16_raw(path, start_sample, max_symbols * sps)
    usable = (c.size // sps) * sps

    if usable <= 0:
        return (
            np.array([], dtype=np.complex64),
            np.array([], dtype=np.float32),
            np.array([], dtype=np.complex64),
        )

    c = c[:usable]
    blocks_view = c.reshape((-1, sps))
    complex_symbols = blocks_view.mean(axis=1).astype(np.complex64)
    mag_symbols = np.mean(np.abs(blocks_view) ** 2, axis=1).astype(np.float32)

    return complex_symbols, mag_symbols, c


def normalize_complex(x: np.ndarray) -> np.ndarray:
    # Purpose: Normalize complex samples to unit average power while preserving phase relationships.
    #
    # Return: A complex64 NumPy array scaled by sqrt(mean(|x|^2)) when power is valid.
    #
    if x.size == 0:
        return x.astype(np.complex64)
    p = float(np.mean(np.abs(x) ** 2))
    if p <= 0.0 or not math.isfinite(p):
        return x.astype(np.complex64)
    return (x / math.sqrt(p)).astype(np.complex64)


def normalize_float(x: np.ndarray) -> np.ndarray:
    # Purpose: Normalize real-valued samples into a 0-to-1 range using their minimum and maximum values.
    #
    # Return: A float32 NumPy array scaled to [0, 1] when the input range is valid.
    #
    if x.size == 0:
        return x.astype(np.float32)
    lo = float(np.min(x))
    hi = float(np.max(x))
    if not math.isfinite(lo) or not math.isfinite(hi) or hi <= lo:
        return x.astype(np.float32)
    return ((x - lo) / (hi - lo)).astype(np.float32)


def run_ook_symbol_slicer(mag_symbols: np.ndarray, normalize: bool) -> list[int]:
    # Purpose: Demodulate simple OOK/ASK symbol-power values by thresholding each symbol into a binary decision.
    #
    # Return: A list of 0/1 bit decisions produced by the GNU Radio threshold and byte-conversion blocks.
    #
    # Equation: b_k = 1 if P_k >= T else 0, where P_k = mean(|x_k[n]|^2) and T = min(P) + 0.5(max(P) - min(P)).
    #
    if mag_symbols.size == 0:
        return []

    x = normalize_float(mag_symbols) if normalize else mag_symbols.astype(np.float32)
    lo = float(np.min(x))
    hi = float(np.max(x))
    if hi <= lo:
        return []

    threshold = lo + 0.5 * (hi - lo)

    # GNU Radio block path for the basic ASK/OOK symbol slicer.
    tb = gr.top_block()
    src = blocks.vector_source_f(x.tolist(), False)
    thr = blocks.threshold_ff(threshold, threshold, 0)
    f2b = blocks.float_to_uchar()
    sink = blocks.vector_sink_b()
    tb.connect(src, thr)
    tb.connect(thr, f2b)
    tb.connect(f2b, sink)
    tb.run()

    return [int(v) & 1 for v in sink.data()]


def merge_short_runs(runs: list[tuple[int, int]], min_len: int) -> list[tuple[int, int]]:
    # Purpose: Merge short state runs into the previous run to suppress tiny threshold fragments.
    #
    # Return: A list of (state, length) runs with adjacent equal states coalesced after short-run merging.
    #
    if not runs or min_len <= 1:
        return runs

    out: list[tuple[int, int]] = []
    for state, length in runs:
        if length < min_len and out:
            prev_state, prev_len = out[-1]
            out[-1] = (prev_state, prev_len + length)
        else:
            out.append((state, length))

    merged: list[tuple[int, int]] = []
    for state, length in out:
        if merged and merged[-1][0] == state:
            prev_state, prev_len = merged[-1]
            merged[-1] = (prev_state, prev_len + length)
        else:
            merged.append((state, length))
    return merged


def build_runs_from_states(states: np.ndarray) -> list[tuple[int, int]]:
    # Purpose: Convert a binary or discrete state array into consecutive run-length segments.
    #
    # Return: A list of (state, length) tuples representing each contiguous state run.
    #
    if states.size == 0:
        return []

    runs: list[tuple[int, int]] = []
    cur = int(states[0])
    count = 1
    for val in states[1:]:
        v = int(val)
        if v == cur:
            count += 1
        else:
            runs.append((cur, count))
            cur = v
            count = 1
    runs.append((cur, count))
    return runs


def threshold_binary_robust(x: np.ndarray) -> np.ndarray:
    # Purpose: Convert real-valued samples into binary states using percentile-based thresholds that avoid outlier-heavy min/max decisions.
    #
    # Return: A uint8 NumPy array of 0/1 states, or an empty array when no valid threshold exists.
    #
    """Return 0/1 states using percentile thresholds instead of raw min/max."""
    if x.size == 0:
        return np.array([], dtype=np.uint8)

    vals = x.astype(np.float32)
    for low_pct, high_pct in ((5, 95), (10, 90), (15, 85), (20, 80), (25, 75)):
        lo = float(np.percentile(vals, low_pct))
        hi = float(np.percentile(vals, high_pct))
        if math.isfinite(lo) and math.isfinite(hi) and hi > lo:
            threshold = lo + 0.5 * (hi - lo)
            states = (vals >= threshold).astype(np.uint8)
            # A useful OOK threshold should produce at least one transition and
            # should not classify nearly everything as one state.
            ones = int(np.count_nonzero(states))
            if 0 < ones < states.size and np.count_nonzero(np.diff(states)) > 0:
                return states

    lo = float(np.min(vals))
    hi = float(np.max(vals))
    if math.isfinite(lo) and math.isfinite(hi) and hi > lo:
        threshold = lo + 0.5 * (hi - lo)
        return (vals >= threshold).astype(np.uint8)
    return np.array([], dtype=np.uint8)


def decode_pwm_pairs_from_runs(runs: list[tuple[int, int]], min_len: int) -> list[int]:
    # Purpose: Decode PWM-style OOK run pairs by comparing high-pulse length against following low-gap length.
    #
    # Return: A list of bits where longer high-than-low pairs map to 1 and shorter high-than-low pairs map to 0.
    #
    # Equation: b_i = 1 if L_high,i > L_low,i else 0 for valid high-low run pairs.
    #
    bits: list[int] = []
    i = 0
    min_len = max(1, min_len)

    while i + 1 < len(runs):
        state_a, len_a = runs[i]
        state_b, len_b = runs[i + 1]

        if state_a == 1 and state_b == 0:
            # Ignore tiny fragments, but accept long sync-ish pulses.
            if len_a >= min_len and len_b >= min_len:
                bits.append(1 if len_a > len_b else 0)
            i += 2
            continue

        # Skip leading/trailing gaps or bad alignment, then resync on next high.
        i += 1

    return bits


def run_ook_raw_pulse_decoder(samples: np.ndarray, sps: int, normalize: bool) -> list[int]:
    # Purpose: Demodulate PWM-style OOK RAW samples by envelope detection, smoothing, thresholding, and high-low run comparison.
    #
    # Return: A list of recovered 0/1 bits from direct sample runs or slot-quantized fallback runs.
    #
    # Equation: E[n] = |x[n]|^2 and b_i = 1 if L_high,i > L_low,i else 0 after thresholding E[n].
    #
    """
    Decode PWM-style OOK RAW pulses by measuring high/low runs.

    For common Flipper RAW patterns like +750 -250 and +250 -750, this maps
    high>low to 1 and high<low to 0. Invert in the C UI if the mapping is wrong.

    This version is intentionally more tolerant than the basic symbol slicer:
    it smooths the envelope, uses percentile thresholds, then falls back to a
    base-slot decoder when run-pair extraction is too sparse. That prevents the
    UI from returning an empty bitstream for clean Flipper RAW captures where the
    threshold or starting phase is slightly off.
    """
    if samples.size < 2 or sps <= 0:
        return []

    env = (np.abs(samples) ** 2).astype(np.float32)
    smooth_len = max(1, min(max(1, sps // 20), 128))
    env_smooth = moving_average(env, smooth_len)
    env_smooth = normalize_float(env_smooth) if normalize else env_smooth

    # First path: direct run-length measurement in sample units.
    states = threshold_binary_robust(env_smooth)
    runs = build_runs_from_states(states)
    runs = merge_short_runs(runs, max(1, sps // 10))
    bits = decode_pwm_pairs_from_runs(runs, max(1, sps // 5))
    if len(bits) >= 2:
        return bits

    # Fallback path: quantize to the user's base timing unit first.
    usable = (env_smooth.size // sps) * sps
    if usable <= 0:
        return bits

    slot_vals = env_smooth[:usable].reshape((-1, sps)).mean(axis=1).astype(np.float32)
    slot_states = threshold_binary_robust(slot_vals)
    slot_runs = build_runs_from_states(slot_states)
    slot_runs = merge_short_runs(slot_runs, 1)
    slot_bits = decode_pwm_pairs_from_runs(slot_runs, 1)

    if len(slot_bits) > len(bits):
        return slot_bits
    return bits


def phase_discriminator(samples: np.ndarray) -> np.ndarray:
    # Purpose: Demodulate instantaneous phase change from complex IQ samples for FM/FSK-style frequency decisions.
    #
    # Return: A float32 NumPy array of per-sample phase differences in radians.
    #
    # Equation: phi[n] = angle(x[n] * conj(x[n-1])).
    #
    if samples.size < 2:
        return np.array([], dtype=np.float32)
    x = normalize_complex(samples)
    return np.angle(x[1:] * np.conj(x[:-1])).astype(np.float32)


def sstv_fm_audio(samples: np.ndarray, input_rate: float) -> tuple[np.ndarray, float]:
    # Purpose: FM-demodulate IQ and decimate the resulting SSTV audio for tone detection.
    #
    # Return: FM-demodulated audio samples and their effective sample rate.
    #
    discriminator = phase_discriminator(samples).astype(np.float64)
    if discriminator.size == 0:
        return np.array([], dtype=np.float32), input_rate

    discriminator -= np.mean(discriminator)

    target_rate = 24000.0
    decimation = max(1, int(math.floor(input_rate / target_rate)))
    usable = (discriminator.size // decimation) * decimation
    if usable <= 0:
        return np.array([], dtype=np.float32), input_rate

    audio = discriminator[:usable].reshape((-1, decimation)).mean(axis=1)
    audio -= np.mean(audio)

    peak = float(np.max(np.abs(audio)))
    if peak > 0.0 and math.isfinite(peak):
        audio /= peak

    return audio.astype(np.float32), input_rate / float(decimation)


def sstv_tone_power(samples: np.ndarray, sample_rate: float, frequency: float) -> float:
    # Purpose: Measure how much of one SSTV header/VIS tone exists in a sample interval.
    #
    # Return: Correlation power at the requested frequency.
    #
    if samples.size < 8:
        return 0.0

    x = samples.astype(np.float64)
    x -= np.mean(x)
    x *= np.hanning(x.size)

    n = np.arange(x.size, dtype=np.float64)
    angle = 2.0 * math.pi * frequency * n / sample_rate
    real = float(np.dot(x, np.cos(angle)))
    imag = float(np.dot(x, np.sin(angle)))

    return (real * real) + (imag * imag)


def sstv_tone_score(
    audio: np.ndarray,
    sample_rate: float,
    start_seconds: float,
    duration_seconds: float,
    target_frequency: float,
) -> float:
    # Purpose: Compare one expected SSTV tone against the other VIS/header tones.
    #
    # Return: Fraction of candidate-tone power belonging to the target frequency.
    #
    start = int(round(start_seconds * sample_rate))
    end = int(round((start_seconds + duration_seconds) * sample_rate))
    if start < 0 or end > audio.size or end - start < 8:
        return 0.0

    segment = audio[start:end]
    frequencies = (1100.0, 1200.0, 1300.0, 1900.0)
    powers = [sstv_tone_power(segment, sample_rate, f) for f in frequencies]
    total = sum(powers) + 1e-18
    target_index = frequencies.index(target_frequency)

    return powers[target_index] / total


def run_sstv_vis(samples: np.ndarray, sps: int) -> list[int]:
    # Purpose: FM-demodulate SSTV IQ, locate the standard VIS header, and recover its VIS bits.
    #
    # Return: Seven transmitted VIS data bits followed by the even-parity bit.
    #
    if samples.size < 2 or sps <= 0:
        return []

    # One VIS bit is 30 ms, so Samples/Symbol represents one 30 ms VIS bit.
    input_rate = float(sps) / 0.030
    audio, audio_rate = sstv_fm_audio(samples, input_rate)

    # 300 ms leader + 10 ms break + 300 ms leader + 30 ms start +
    # 7 x 30 ms VIS bits + 30 ms parity + 30 ms stop = 910 ms.
    header_seconds = 0.910
    required = int(math.ceil(header_seconds * audio_rate))
    if audio.size < required:
        return []

    step = max(1, int(round(0.005 * audio_rate)))
    last_start = audio.size - required
    best_score = 0.0
    best_start = None

    for start in range(0, last_start + 1, step):
        t = float(start) / audio_rate
        score = (
            sstv_tone_score(audio, audio_rate, t + 0.050, 0.200, 1900.0)
            + sstv_tone_score(audio, audio_rate, t + 0.300, 0.010, 1200.0)
            + sstv_tone_score(audio, audio_rate, t + 0.360, 0.200, 1900.0)
            + sstv_tone_score(audio, audio_rate, t + 0.615, 0.020, 1200.0)
        ) / 4.0

        if score > best_score:
            best_score = score
            best_start = t

    if best_start is None or best_score < 0.55:
        return []

    bit_start = best_start + 0.640
    bits: list[int] = []

    for bit_index in range(7):
        t = bit_start + (float(bit_index) * 0.030) + 0.005
        one_score = sstv_tone_score(audio, audio_rate, t, 0.020, 1100.0)
        zero_score = sstv_tone_score(audio, audio_rate, t, 0.020, 1300.0)
        bits.append(1 if one_score > zero_score else 0)

    parity_time = bit_start + (7.0 * 0.030) + 0.005
    parity_one = sstv_tone_score(audio, audio_rate, parity_time, 0.020, 1100.0)
    parity_zero = sstv_tone_score(audio, audio_rate, parity_time, 0.020, 1300.0)
    parity = 1 if parity_one > parity_zero else 0

    stop_time = bit_start + (8.0 * 0.030) + 0.005
    stop_score = sstv_tone_score(audio, audio_rate, stop_time, 0.020, 1200.0)

    if ((sum(bits) + parity) & 1) != 0:
        return []
    if stop_score < 0.45:
        return []

    bits.append(parity)
    return bits


def moving_average(x: np.ndarray, n: int) -> np.ndarray:
    # Purpose: Smooth a real-valued sequence with a rectangular moving-average kernel.
    #
    # Return: A float32 NumPy array containing the same-mode convolution result.
    #
    if x.size == 0 or n <= 1:
        return x.astype(np.float32)
    n = min(n, x.size)
    kernel = np.ones(n, dtype=np.float32) / float(n)
    return np.convolve(x, kernel, mode="same").astype(np.float32)


def symbol_average_float(x: np.ndarray, sps: int, max_symbols: int) -> np.ndarray:
    # Purpose: Convert a real-valued sample stream into symbol-spaced values by averaging fixed samples-per-symbol blocks.
    #
    # Return: A float32 NumPy array of at most max_symbols averaged symbol values.
    #
    if x.size == 0 or sps <= 0:
        return np.array([], dtype=np.float32)
    usable = (x.size // sps) * sps
    if usable <= 0:
        return np.array([], dtype=np.float32)
    vals = x[:usable].reshape((-1, sps)).mean(axis=1).astype(np.float32)
    return vals[:max_symbols]


def run_fsk2(samples: np.ndarray, sps: int, max_symbols: int, gfsk: bool = False) -> list[int]:
    # Purpose: Demodulate 2-FSK/GFSK/AFSK-style IQ by phase discrimination, optional smoothing, symbol averaging, and median slicing.
    #
    # Return: A list of 0/1 bit decisions from the symbol-averaged discriminator output.
    #
    # Equation: b_k = 1 if v_k >= median(v) else 0, where v_k = mean(angle(x[n] * conj(x[n-1]))) over symbol k.
    #
    freq = phase_discriminator(samples)
    if gfsk:
        freq = moving_average(freq, max(3, sps // 3))
    vals = symbol_average_float(freq, sps, max_symbols)
    if vals.size == 0:
        return []
    threshold = float(np.median(vals))
    return [1 if v >= threshold else 0 for v in vals]


def kmeans_1d(vals: np.ndarray, k: int, rounds: int = 24) -> np.ndarray:
    # Purpose: Estimate one-dimensional cluster centers for multi-level symbol decisions.
    #
    # Return: A sorted float32 NumPy array containing k estimated cluster centers.
    #
    if vals.size == 0:
        return np.array([], dtype=np.float32)
    if vals.size < k:
        lo = float(np.min(vals))
        hi = float(np.max(vals))
        return np.linspace(lo, hi, k, dtype=np.float32)

    centers = np.percentile(vals, np.linspace(5, 95, k)).astype(np.float32)
    for _ in range(rounds):
        dist = np.abs(vals[:, None] - centers[None, :])
        labels = np.argmin(dist, axis=1)
        new_centers = centers.copy()
        for idx in range(k):
            part = vals[labels == idx]
            if part.size:
                new_centers[idx] = float(np.mean(part))
        if np.allclose(new_centers, centers):
            break
        centers = new_centers
    return np.sort(centers)


def detect_sps_from_intervals(intervals: np.ndarray) -> Optional[int]:
    # Purpose: Estimate a fundamental symbol period from transition spacings that may span one or more symbols.
    #
    # Return: Estimated integer samples-per-symbol, or None when the intervals do not support a stable period.
    #
    # The score rewards a candidate that explains many observed run lengths as integer multiples while also
    # preferring candidates that are directly represented by one-symbol runs.
    vals = np.asarray(intervals, dtype=np.float64)
    vals = vals[np.isfinite(vals) & (vals >= 2.0)]
    if vals.size < 2:
        return None

    vals = np.sort(vals)
    if vals.size > 2048:
        # Timing information is concentrated in the shorter transition spacings; very long runs add little
        # information and can dominate the candidate set.
        vals = vals[:2048]

    median_len = float(np.median(vals))
    if not math.isfinite(median_len) or median_len < 2.0:
        return None

    # Remove only extreme tiny fragments. Legitimate one-symbol runs are intentionally retained.
    tiny_floor = max(2.0, median_len * 0.04)
    filtered = vals[vals >= tiny_floor]
    if filtered.size >= max(4, vals.size // 3):
        vals = filtered

    seed_vals = vals[: min(vals.size, 256)]
    candidates: set[int] = set()

    for value in seed_vals:
        max_multiple = min(32, max(1, int(round(value / 2.0))))
        for multiple in range(1, max_multiple + 1):
            candidate = int(round(value / float(multiple)))
            if candidate >= 2:
                candidates.add(candidate)

    if not candidates:
        return None

    best_sps = None
    best_score = -1.0e30

    for candidate in candidates:
        ratios = vals / float(candidate)
        nearest = np.rint(ratios)
        valid = (nearest >= 1.0) & (nearest <= 128.0)
        if not np.any(valid):
            continue

        residual = np.abs(ratios - nearest)
        close = valid & (residual <= 0.12)
        coverage = float(np.count_nonzero(close)) / float(vals.size)
        if coverage < 0.35:
            continue

        one_support = float(np.count_nonzero(close & (nearest == 1.0))) / float(vals.size)
        primitive_support = float(np.count_nonzero(close & (nearest <= 2.0))) / float(vals.size)
        valid_residuals = residual[close]
        median_residual = float(np.median(valid_residuals)) if valid_residuals.size else 1.0

        score = (4.0 * coverage) + (3.0 * one_support) + (0.35 * primitive_support) - median_residual

        if score > best_score or (math.isclose(score, best_score, rel_tol=1e-12, abs_tol=1e-12) and
                                  (best_sps is None or candidate > best_sps)):
            best_score = score
            best_sps = candidate

    return best_sps


def refine_sps_from_events(events: np.ndarray, coarse_sps: int) -> int:
    # Purpose: Refine a coarse SPS estimate by finding the period that best phase-aligns observed transitions.
    #
    # Return: Refined integer samples-per-symbol.
    if coarse_sps < 2:
        return max(2, coarse_sps)

    positions = np.asarray(events, dtype=np.float64)
    positions = positions[np.isfinite(positions)]
    if positions.size < 3:
        return coarse_sps

    low = max(2, int(math.floor(coarse_sps * 0.80)))
    high = max(low, int(math.ceil(coarse_sps * 1.20)))

    def coherence(candidate: int) -> float:
        phases = (2.0 * math.pi / float(candidate)) * positions
        vector = np.exp(1j * phases)
        return float(abs(np.mean(vector)))

    width = high - low
    step = max(1, int(math.ceil(width / 320.0)))
    best = coarse_sps
    best_score = -1.0

    for candidate in range(low, high + 1, step):
        score = coherence(candidate)
        if score > best_score:
            best_score = score
            best = candidate

    fine_low = max(low, best - (2 * step))
    fine_high = min(high, best + (2 * step))

    for candidate in range(fine_low, fine_high + 1):
        score = coherence(candidate)
        if score > best_score:
            best_score = score
            best = candidate

    return best


def detect_binary_state_sps(values: np.ndarray, smooth_len: int = 1) -> Optional[int]:
    # Purpose: Detect SPS from a two-state amplitude or frequency-domain timing signal.
    #
    # Return: Estimated integer samples-per-symbol, or None.
    if values.size < 16:
        return None

    x = values.astype(np.float32)
    if smooth_len > 1:
        x = moving_average(x, smooth_len)

    states = threshold_binary_robust(x)
    if states.size < 16:
        return None

    events = np.flatnonzero(states[1:] != states[:-1]).astype(np.int64) + 1
    if events.size < 3:
        return None

    coarse = detect_sps_from_intervals(np.diff(events))
    if coarse is None:
        return None

    return refine_sps_from_events(events, coarse)


def detect_ook_sps(samples: np.ndarray, hint_sps: int) -> Optional[int]:
    # Purpose: Detect ASK/OOK symbol timing from envelope transitions.
    #
    # Return: Estimated integer samples-per-symbol, or None.
    if samples.size < 16:
        return None

    envelope = (np.abs(samples) ** 2).astype(np.float32)
    smooth_len = max(1, min(17, max(1, hint_sps // 40)))
    return detect_binary_state_sps(envelope, smooth_len)


def scalar_transition_events(values: np.ndarray) -> np.ndarray:
    # Purpose: Locate likely symbol boundaries from sharp changes in a real-valued timing signal.
    #
    # Return: Integer sample positions of transition peaks.
    if values.size < 32:
        return np.array([], dtype=np.int64)

    energy = np.abs(np.diff(values.astype(np.float32))).astype(np.float32)
    energy = moving_average(energy, 3)
    if energy.size < 16 or float(np.max(energy)) <= 0.0:
        return np.array([], dtype=np.int64)

    selected = np.array([], dtype=np.int64)
    minimum_events = min(200, max(12, energy.size // 1000))

    for percentile in (99.9, 99.5, 99.0, 98.0, 97.0, 95.0, 92.0, 90.0):
        threshold = float(np.percentile(energy, percentile))
        if not math.isfinite(threshold) or threshold <= 0.0:
            continue

        mask = energy >= threshold
        changes = np.diff(mask.astype(np.int8), prepend=0, append=0)
        starts = np.flatnonzero(changes == 1)
        ends = np.flatnonzero(changes == -1)
        events: list[int] = []

        for start, end in zip(starts, ends):
            if end <= start:
                continue
            local = energy[start:end]
            peak = start + int(np.argmax(local)) + 1
            events.append(peak)

        if len(events) >= 6:
            candidate_events = np.asarray(events, dtype=np.int64)
            if candidate_events.size > selected.size:
                selected = candidate_events
            if minimum_events <= candidate_events.size <= 5000:
                selected = candidate_events
                break

    return selected


def detect_fsk_sps(samples: np.ndarray, mod: str, hint_sps: int) -> Optional[int]:
    # Purpose: Detect FSK-family symbol timing from phase-discriminator states without assuming the user's SPS.
    #
    # Return: Estimated integer samples-per-symbol, or None.
    discriminator = phase_discriminator(samples)
    if discriminator.size < 16:
        return None

    discriminator = discriminator - float(np.median(discriminator))

    # A small fixed detector-only smoothing window suppresses per-sample discriminator jitter. It is deliberately
    # independent of hint_sps: the existing Samples/Symbol field must not scale or constrain blind detection.
    if mod == "gfsk":
        smooth_len = 9
    else:
        smooth_len = 5

    discriminator = moving_average(discriminator, smooth_len)

    if mod != "fsk4":
        return detect_binary_state_sps(discriminator, 1)

    # 4-FSK has four frequency states, so a binary threshold loses transitions between same-side levels.
    # Cluster the discriminator into four levels and recover timing from every level transition instead.
    stride = max(1, discriminator.size // 50000)
    centers = kmeans_1d(discriminator[::stride], 4)
    if centers.size != 4:
        return None

    labels = np.argmin(np.abs(discriminator[:, None] - centers[None, :]), axis=1).astype(np.int16)
    events = np.flatnonzero(labels[1:] != labels[:-1]).astype(np.int64) + 1
    if events.size < 3:
        return None

    coarse = detect_sps_from_intervals(np.diff(events))
    if coarse is None:
        return None

    return refine_sps_from_events(events, coarse)


def complex_transition_events(samples: np.ndarray) -> np.ndarray:
    # Purpose: Locate likely PSK/QAM symbol-boundary events after removing residual carrier rotation.
    #
    # Return: Integer sample positions of transition-energy peaks.
    if samples.size < 32:
        return np.array([], dtype=np.int64)

    x = normalize_complex(samples).astype(np.complex64)
    products = x[1:] * np.conj(x[:-1])
    product_sum = np.sum(products.astype(np.complex128))
    residual_rotation = float(np.angle(product_sum)) if abs(product_sum) > 1e-18 else 0.0

    if math.isfinite(residual_rotation) and abs(residual_rotation) > 1e-12:
        n = np.arange(x.size, dtype=np.float64)
        x = (x * np.exp(-1j * residual_rotation * n)).astype(np.complex64)

    energy = (np.abs(np.diff(x)) ** 2).astype(np.float32)
    energy = moving_average(energy, 3)

    if energy.size < 16 or float(np.max(energy)) <= 0.0:
        return np.array([], dtype=np.int64)

    # Symbol changes are outliers in transition energy; within-symbol RF noise is not. A MAD threshold is much
    # more stable than taking a fixed percentile when the recording contains hundreds or thousands of samples
    # per symbol, because real boundaries may occupy far below 0.1 percent of the samples.
    median_energy = float(np.median(energy))
    mad = float(np.median(np.abs(energy - median_energy)))
    if not math.isfinite(median_energy) or not math.isfinite(mad):
        return np.array([], dtype=np.int64)

    mad = max(mad, 1e-18)
    selected = np.array([], dtype=np.int64)

    for multiplier in (50.0, 30.0, 20.0, 12.0, 8.0, 6.0):
        threshold = median_energy + multiplier * mad
        mask = energy >= threshold
        changes = np.diff(mask.astype(np.int8), prepend=0, append=0)
        starts = np.flatnonzero(changes == 1)
        ends = np.flatnonzero(changes == -1)
        events: list[int] = []

        for event_start, event_end in zip(starts, ends):
            if event_end <= event_start:
                continue
            local = energy[event_start:event_end]
            peak = event_start + int(np.argmax(local)) + 1
            events.append(peak)

        if len(events) >= 6:
            selected = np.asarray(events, dtype=np.int64)
            if selected.size <= 5000:
                return selected

    return selected


def detect_constellation_sps(samples: np.ndarray) -> Optional[int]:
    # Purpose: Detect PSK/QAM SPS from complex-IQ transition spacing and folded transition phase.
    #
    # Return: Estimated integer samples-per-symbol, or None.
    events = complex_transition_events(samples)
    if events.size < 3:
        return None

    coarse = detect_sps_from_intervals(np.diff(events))
    if coarse is None:
        return None

    return refine_sps_from_events(events, coarse)


def detect_sstv_vis_sps(samples: np.ndarray) -> Optional[int]:
    # Purpose: Detect the SSTV VIS 30 ms symbol length from the 300/10/300 ms leader-break-leader timing pattern.
    #
    # Return: Estimated samples in one 30 ms VIS bit, or None.
    audio = phase_discriminator(samples).astype(np.float32)
    if audio.size < 128:
        return None

    audio = audio - float(np.mean(audio))
    signs = audio >= 0.0
    crossings = np.flatnonzero(signs[1:] != signs[:-1]).astype(np.int64) + 1
    if crossings.size < 20:
        return None

    half_periods = np.diff(crossings).astype(np.float32)
    valid = (half_periods >= 2.0) & np.isfinite(half_periods)
    if np.count_nonzero(valid) < 16:
        return None

    # Average several zero-crossing intervals so integer sample quantization of one tone does not become
    # multiple artificial clusters (for example alternating 26/27-sample half periods at 1900 Hz).
    smoothed_half_periods = moving_average(half_periods, 15)
    hp = smoothed_half_periods[valid]
    stride = max(1, hp.size // 50000)
    centers = kmeans_1d(hp[::stride], 4)
    if centers.size != 4:
        return None

    all_labels = np.argmin(np.abs(smoothed_half_periods[:, None] - centers[None, :]), axis=1).astype(np.int16)

    runs: list[tuple[int, int, int]] = []
    run_start = 0
    current = int(all_labels[0])

    for index in range(1, all_labels.size):
        label = int(all_labels[index])
        if label != current:
            duration = int(crossings[index] - crossings[run_start])
            interval_count = index - run_start
            runs.append((current, duration, interval_count))
            current = label
            run_start = index

    duration = int(crossings[-1] - crossings[run_start])
    interval_count = all_labels.size - run_start
    runs.append((current, duration, interval_count))

    # First use the bridge states created by the smoothing window to estimate the physical tone-change
    # boundaries at the middle of each bridge. This keeps the detected 30 ms SPS from inheriting the
    # smoothing-window shortening of the stable tone runs.
    bridge_best_sps = None
    bridge_best_error = 1.0e30

    for index in range(0, len(runs) - 6):
        state0, d0, _ = runs[index]
        bridge1, db1, _ = runs[index + 1]
        state1, d1, _ = runs[index + 2]
        bridge2, db2, _ = runs[index + 3]
        state2, d2, _ = runs[index + 4]
        bridge3, db3, _ = runs[index + 5]
        state3, _, _ = runs[index + 6]

        if state0 != state2 or state1 != state3 or state0 == state1:
            continue
        if bridge1 != bridge2 or bridge2 != bridge3:
            continue
        if bridge1 == state0 or bridge1 == state1:
            continue

        corrected_break = float(d1) + (0.5 * float(db1 + db2))
        corrected_second_leader = float(d2) + (0.5 * float(db2 + db3))
        if corrected_break <= 0.0 or corrected_second_leader <= 0.0:
            continue

        unit = corrected_second_leader / 30.0
        expected_break = unit
        error = abs(corrected_break - expected_break) / max(expected_break, 1.0)

        if error < bridge_best_error:
            bridge_best_error = error
            bridge_best_sps = max(2, int(round(corrected_second_leader / 10.0)))

    if bridge_best_sps is not None and bridge_best_error <= 0.20:
        return bridge_best_sps

    # Ignore short bridge states introduced by the zero-crossing smoothing window. A real 10 ms SSTV break
    # contains roughly two dozen half cycles at 1200 Hz, independent of the capture sample rate.
    stable_runs = [(state, duration) for state, duration, count in runs if count >= 15]

    best_unit = None
    best_error = 1.0e30

    for index in range(0, len(stable_runs) - 2):
        state0, d0 = stable_runs[index]
        state1, d1 = stable_runs[index + 1]
        state2, d2 = stable_runs[index + 2]

        if state0 != state2 or state0 == state1:
            continue
        if min(d0, d1, d2) <= 0:
            continue

        estimates = np.array([d0 / 30.0, float(d1), d2 / 30.0], dtype=np.float64)
        unit = float(np.median(estimates))
        if not math.isfinite(unit) or unit < 1.0:
            continue

        expected = np.array([30.0, 1.0, 30.0], dtype=np.float64) * unit
        actual = np.array([d0, d1, d2], dtype=np.float64)
        relative_error = float(np.mean(np.abs(actual - expected) / np.maximum(expected, 1.0)))

        if relative_error < best_error:
            best_error = relative_error
            best_unit = unit

    if best_unit is not None and best_error <= 0.35:
        return max(2, int(round(best_unit * 3.0)))

    # Fallback: stable VIS/data tone runs are normally 30 ms or integer multiples of 30 ms.
    tone_durations = np.asarray([duration for _, duration in stable_runs if duration >= 2], dtype=np.float64)
    return detect_sps_from_intervals(tone_durations)

def select_active_timing_region(samples: np.ndarray) -> np.ndarray:
    # Purpose: Isolate the strongest contiguous RF burst before blind SPS detection so long idle/noise regions
    #          cannot dominate transition timing and collapse the estimate to 2 or 3 samples/symbol.
    #
    # Return: A view/copy of the most useful active timing region, or the original samples when no clear burst exists.
    if samples.size < 4096:
        return samples

    power = (np.abs(samples) ** 2).astype(np.float32)

    # Use coarse power blocks only for burst localization. This does not assume a symbol period.
    block = max(64, min(512, max(64, samples.size // 2048)))
    usable = (power.size // block) * block
    if usable < block * 8:
        return samples

    block_power = power[:usable].reshape((-1, block)).mean(axis=1).astype(np.float64)
    block_db = 10.0 * np.log10(block_power + 1e-18)

    low_db = float(np.percentile(block_db, 20.0))
    high_db = float(np.percentile(block_db, 99.9))
    if not math.isfinite(low_db) or not math.isfinite(high_db) or high_db - low_db < 4.0:
        return samples

    threshold_db = low_db + 0.45 * (high_db - low_db)
    active = np.flatnonzero(block_db >= threshold_db)
    if active.size < 2:
        return samples

    # Join nearby active islands so ASK/OOK zero runs remain inside one burst. The gap is expressed in coarse
    # power blocks rather than SPS, so this localization step remains independent of the user's SPS field.
    join_gap = max(8, min(128, block_db.size // 32))
    split_points = np.flatnonzero(np.diff(active) > join_gap) + 1
    groups = np.split(active, split_points)

    best_group = None
    best_score = -1.0

    for group in groups:
        if group.size == 0:
            continue

        excess = np.maximum(block_db[group] - threshold_db, 0.0)
        score = float(np.sum(excess)) * math.sqrt(float(group.size))

        if score > best_score:
            best_score = score
            best_group = group

    if best_group is None or best_group.size == 0:
        return samples

    start = int(best_group[0]) * block
    end = min(samples.size, (int(best_group[-1]) + 1) * block)
    if end <= start:
        return samples

    # Keep the selected burst boundaries tight so edge noise cannot dominate blind timing detection.
    padding = 0
    start = max(0, start - padding)
    end = min(samples.size, end + padding)

    if end - start < 256:
        return samples

    return samples[start:end]


def trim_continuous_carrier_region(samples: np.ndarray) -> np.ndarray:
    # Purpose: Remove low-power edge noise for continuously keyed modulations before timing analysis.
    #
    # Return: Longest sustained high-power run, or the original region when no clear carrier run exists.
    if samples.size < 256:
        return samples

    power = moving_average((np.abs(samples) ** 2).astype(np.float32), 17)
    low = float(np.percentile(power, 0.1))
    high = float(np.percentile(power, 50.0))
    if not math.isfinite(low) or not math.isfinite(high) or high <= low:
        return samples

    if high < max(low * 4.0, low + 1e-12):
        return samples

    threshold = low + 0.30 * (high - low)
    states = power >= threshold
    changes = np.diff(states.astype(np.int8), prepend=0, append=0)
    starts = np.flatnonzero(changes == 1)
    ends = np.flatnonzero(changes == -1)
    if starts.size == 0 or ends.size == 0:
        return samples

    lengths = ends - starts
    best_index = int(np.argmax(lengths))
    start = int(starts[best_index])
    end = int(ends[best_index])

    if end - start < 128:
        return samples

    return samples[start:end]


def detect_samples_per_symbol(samples: np.ndarray, mod: str, hint_sps: int) -> Optional[int]:
    # Purpose: Dispatch modulation-aware automatic Samples/Symbol detection without changing normal demodulation.
    #
    # Return: Estimated integer samples-per-symbol, or None.
    if samples.size < 16:
        return None

    timing_samples = select_active_timing_region(samples)

    if mod in {"ook", "ook_raw"}:
        return detect_ook_sps(timing_samples, hint_sps)

    if mod in {"fsk2", "gfsk", "fsk4", "afsk"}:
        timing_samples = trim_continuous_carrier_region(timing_samples)
        return detect_fsk_sps(timing_samples, mod, hint_sps)

    if mod in {"bpsk", "qpsk", "psk8", "qam16", "qam64"}:
        timing_samples = trim_continuous_carrier_region(timing_samples)
        return detect_constellation_sps(timing_samples)

    if mod == "sstv_vis":
        timing_samples = trim_continuous_carrier_region(timing_samples)
        return detect_sstv_vis_sps(timing_samples)

    return None


def detection_sample_count(path: Path, start_sample: int, hint_sps: int, mod: str) -> int:
    # Purpose: Choose a bounded amount of IQ for SPS detection while respecting the selected start sample.
    #
    # Return: Number of complex IQ samples to inspect.
    total_samples = max(0, path.stat().st_size // 4)
    remaining = max(0, total_samples - max(0, start_sample))
    if remaining <= 0:
        return 0

    base = 4_000_000 if mod == "sstv_vis" else 1_000_000
    hinted = max(0, hint_sps) * 512
    target = max(base, min(4_000_000, hinted))
    return int(min(remaining, target))


def run_fsk4(samples: np.ndarray, sps: int, max_symbols: int) -> list[int]:
    # Purpose: Demodulate 4-FSK IQ by phase discrimination, symbol averaging, four-center clustering, and nearest-center labeling.
    #
    # Return: A list of 2-bit symbol labels represented as integers from 0 to 3.
    #
    # Equation: s_k = argmin_j |v_k - c_j|, where v_k = mean(angle(x[n] * conj(x[n-1]))) over symbol k.
    #
    vals = symbol_average_float(phase_discriminator(samples), sps, max_symbols)
    if vals.size == 0:
        return []
    centers = kmeans_1d(vals, 4)
    labels = np.argmin(np.abs(vals[:, None] - centers[None, :]), axis=1)
    return [int(v) & 0x3 for v in labels]


def constellation_points(mod: str) -> tuple[np.ndarray, int]:
    # Purpose: Provide ideal constellation points and natural bits-per-symbol for supported PSK/QAM modes.
    #
    # Return: A tuple containing a complex64 NumPy array of ideal points and the natural bits-per-symbol value.
    #
    if mod == "bpsk":
        return np.array([-1 + 0j, 1 + 0j], dtype=np.complex64), 1
    if mod == "qpsk":
        return np.array([1 + 1j, -1 + 1j, -1 - 1j, 1 - 1j], dtype=np.complex64), 2
    if mod == "psk8":
        pts = [np.exp(1j * 2.0 * math.pi * n / 8.0) for n in range(8)]
        return np.array(pts, dtype=np.complex64), 3
    if mod == "qam16":
        levels = [-3, -1, 1, 3]
        pts = [complex(i, q) for q in levels for i in levels]
        return np.array(pts, dtype=np.complex64), 4
    if mod == "qam64":
        levels = [-7, -5, -3, -1, 1, 3, 5, 7]
        pts = [complex(i, q) for q in levels for i in levels]
        return np.array(pts, dtype=np.complex64), 6
    raise RuntimeError(f"Unsupported constellation mode: {mod}")


def run_constellation_demod(symbols: np.ndarray, mod: str, normalize: bool) -> tuple[list[int], int]:
    # Purpose: Demodulate PSK/QAM symbols using GNU Radio BPSK/QPSK decoding when available or nearest-constellation fallback decisions.
    #
    # Return: A tuple containing integer symbol labels and the natural bits-per-symbol value for the modulation.
    #
    # Equation: s_k = argmin_j |x_k - p_j|, where x_k is the received symbol and p_j is an ideal constellation point.
    #
    if symbols.size == 0:
        return [], 1

    x = normalize_complex(symbols) if normalize else symbols.astype(np.complex64)

    # Keep the old GNU Radio path for BPSK/QPSK when the installed runtime has
    # the convenience constellation objects. Fall back to distance decisions for
    # portability and for 8PSK/QAM modes.
    if mod in {"bpsk", "qpsk"}:
        try:
            if mod == "bpsk" and hasattr(digital, "constellation_bpsk"):
                const = digital.constellation_bpsk().base()
            elif mod == "qpsk" and hasattr(digital, "constellation_qpsk"):
                const = digital.constellation_qpsk().base()
            else:
                raise RuntimeError("constellation helper unavailable")
            tb = gr.top_block()
            src = blocks.vector_source_c(x.tolist(), False)
            dec = digital.constellation_decoder_cb(const)
            sink = blocks.vector_sink_b()
            tb.connect(src, dec)
            tb.connect(dec, sink)
            tb.run()
            return [int(v) for v in sink.data()], (1 if mod == "bpsk" else 2)
        except Exception as exc:
            print(f"GNU Radio constellation decoder fallback for {mod}: {exc}", file=sys.stderr)

    pts, bps = constellation_points(mod)
    pts = normalize_complex(pts)
    labels = np.argmin(np.abs(x[:, None] - pts[None, :]), axis=1)
    return [int(v) for v in labels], bps


def symbol_to_bits(symbol: int, bps: int, invert: bool) -> str:
    # Purpose: Convert one integer symbol label into a fixed-width binary string with optional bit inversion.
    #
    # Return: A string of bps characters containing only 0 and 1.
    #
    out = []
    for bit_index in range(bps - 1, -1, -1):
        bit = (symbol >> bit_index) & 1
        if invert:
            bit ^= 1
        out.append("1" if bit else "0")
    return "".join(out)


def write_bits(symbols: Iterable[int], bps: int, invert: bool, tight: bool) -> None:
    # Purpose: Format recovered symbols as bits and write the resulting bitstream to standard output.
    #
    # Return: None; the function writes text output directly to stdout.
    #
    sep = "" if tight else " "
    parts = [symbol_to_bits(int(sym), bps, invert) for sym in symbols]
    sys.stdout.write(sep.join(parts))
    sys.stdout.write("\n")


def main() -> int:
    # Purpose: Parse command-line arguments, load IQ samples, dispatch the selected demodulator, and write recovered bits.
    #
    # Return: An integer process status code where 0 indicates success and nonzero values indicate errors.
    #
    ap = argparse.ArgumentParser(description="RetroSpectrum GNU Radio .complex16 demod helper")
    ap.add_argument("--input", required=True)
    ap.add_argument("--mod", choices=MODES, required=True)
    ap.add_argument("--sps", type=int, required=True)
    ap.add_argument("--start-sample", type=int, default=0)
    ap.add_argument("--max-symbols", type=int, default=8192)
    ap.add_argument("--bits-per-symbol", type=int, default=1)
    ap.add_argument("--normalize", type=int, default=1)
    ap.add_argument("--invert", type=int, default=0)
    ap.add_argument("--tight", type=int, default=1)
    ap.add_argument("--detect-sps", action="store_true")
    args = ap.parse_args()

    path = Path(args.input)
    if not path.exists():
        print(f"Input file does not exist: {path}", file=sys.stderr)
        return 1
    if args.sps <= 0:
        print("samples/symbol must be > 0", file=sys.stderr)
        return 1

    if args.detect_sps:
        try:
            sample_count = detection_sample_count(path, args.start_sample, args.sps, args.mod)
            if sample_count <= 0:
                print("No IQ samples are available for SPS detection.", file=sys.stderr)
                return 1
            detect_samples = read_complex16_raw(path, args.start_sample, sample_count)
        except Exception as exc:
            print(f"Could not read IQ samples for SPS detection: {exc}", file=sys.stderr)
            return 1

        detected_sps = detect_samples_per_symbol(detect_samples, args.mod, args.sps)
        if detected_sps is None or detected_sps <= 0:
            print("Could not detect a stable samples/symbol value for the selected modulation.", file=sys.stderr)
            return 1

        sys.stdout.write(f"{detected_sps}\n")
        return 0

    if args.mod == "sstv_vis":
        # For SSTV VIS, Samples/Symbol is the number of IQ samples in one 30 ms VIS bit.
        input_rate = float(args.sps) / 0.030
        search_samples = int(math.ceil(input_rate * 5.0))

        try:
            raw_samples = read_complex16_raw(path, args.start_sample, search_samples)
        except Exception as exc:
            print(f"Could not read SSTV IQ samples: {exc}", file=sys.stderr)
            return 1

        if raw_samples.size == 0:
            print("No SSTV IQ samples were read.", file=sys.stderr)
            return 1

        symbols = run_sstv_vis(raw_samples, args.sps)
        if not symbols:
            print("No valid SSTV VIS header was detected.", file=sys.stderr)
            return 1

        write_bits(symbols, 1, bool(args.invert), bool(args.tight))
        return 0

    try:
        complex_symbols, mag_symbols, raw_samples = symbol_views(
            path, args.start_sample, args.max_symbols, args.sps
        )
    except Exception as exc:
        print(f"Could not read samples: {exc}", file=sys.stderr)
        return 1

    if complex_symbols.size == 0 or raw_samples.size == 0:
        print("No symbols read. Check samples/symbol, start sample, and file length.", file=sys.stderr)
        return 1

    invert = bool(args.invert)
    tight = bool(args.tight)
    user_bps = args.bits_per_symbol

    if args.mod == "ook":
        symbols = run_ook_symbol_slicer(mag_symbols, bool(args.normalize))
        natural_bps = 1
    elif args.mod == "ook_raw":
        symbols = run_ook_raw_pulse_decoder(raw_samples, args.sps, bool(args.normalize))
        natural_bps = 1
    elif args.mod in {"fsk2", "gfsk", "afsk"}:
        symbols = run_fsk2(raw_samples, args.sps, args.max_symbols, gfsk=(args.mod == "gfsk"))
        natural_bps = 1
    elif args.mod == "fsk4":
        symbols = run_fsk4(raw_samples, args.sps, args.max_symbols)
        natural_bps = 2
    elif args.mod in {"bpsk", "qpsk", "psk8", "qam16", "qam64"}:
        symbols, natural_bps = run_constellation_demod(complex_symbols, args.mod, bool(args.normalize))
    else:
        print(f"Unsupported modulation: {args.mod}", file=sys.stderr)
        return 1

    if not symbols:
        print("Demodulator produced no symbols.", file=sys.stderr)
        return 1

    bps = user_bps if 1 <= user_bps <= 8 else natural_bps
    write_bits(symbols, bps, invert, tight)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
