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
from typing import Iterable

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
    args = ap.parse_args()

    path = Path(args.input)
    if not path.exists():
        print(f"Input file does not exist: {path}", file=sys.stderr)
        return 1
    if args.sps <= 0:
        print("samples/symbol must be > 0", file=sys.stderr)
        return 1

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
