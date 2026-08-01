#!/usr/bin/env python3
"""Generate known-answer test vectors for the C unit tests.

The vectors come from the Python reference implementation, not from the C code.
A test built from the implementation it tests only proves the code is
self-consistent; these vectors mean the C has to agree with something written
independently, in a different language, by a different route.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from scipy import signal as sp_signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import config as cfg, preprocess  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "tests" / "test_vectors.h"

N_IMPULSE = 64
N_SYNTH = 3600  # 10 s at 360 Hz


def c_float_array(name: str, values, per_line: int = 6) -> str:
    vals = [f"{float(v):+.9e}f" for v in np.asarray(values).reshape(-1)]
    lines = ["    " + ", ".join(vals[i:i + per_line]) + ","
             for i in range(0, len(vals), per_line)]
    return (f"static const float {name}[{len(vals)}] = {{\n"
            + "\n".join(lines) + "\n};\n\n")


def c_u32_array(name: str, values, per_line: int = 12) -> str:
    vals = [str(int(v)) for v in np.asarray(values).reshape(-1)]
    lines = ["    " + ", ".join(vals[i:i + per_line]) + ","
             for i in range(0, len(vals), per_line)]
    return (f"static const uint32_t {name}[{len(vals)}] = {{\n"
            + "\n".join(lines) + "\n};\n\n")


def synthetic_ecg(n: int, fs: int, bpm: float = 72.0, seed: int = 7):
    """A crude but adequate synthetic ECG: Gaussian P, QRS and T bumps on a
    wandering baseline, with known R-peak positions.

    Synthetic rather than a real record for the unit test because the expected
    answer must be exactly known. Real-record accuracy is measured separately,
    against the reference annotations, in sim/.
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n) / fs
    x = np.zeros(n, dtype=np.float64)

    period = 60.0 / bpm
    r_positions = []
    beat_t = 0.6
    while beat_t < (n / fs) - 0.6:
        r = int(round(beat_t * fs))
        r_positions.append(r)

        def bump(centre_s, amp, width_s):
            c = centre_s * fs
            w = width_s * fs
            idx = np.arange(max(0, int(c - 4 * w)), min(n, int(c + 4 * w)))
            x[idx] += amp * np.exp(-0.5 * ((idx - c) / w) ** 2)

        bump(beat_t - 0.16, 0.12, 0.022)   # P wave
        bump(beat_t - 0.020, -0.12, 0.006)  # Q
        bump(beat_t, 1.00, 0.0075)          # R
        bump(beat_t + 0.022, -0.22, 0.008)  # S
        bump(beat_t + 0.20, 0.28, 0.045)    # T
        beat_t += period * (1.0 + 0.02 * rng.standard_normal())

    x += 0.06 * np.sin(2 * np.pi * 0.25 * t)          # baseline wander
    x += 0.006 * rng.standard_normal(n)                # sensor noise
    return x.astype(np.float32), np.array(r_positions, dtype=np.int64)


def main() -> int:
    parts = ["""/* test_vectors.h -- GENERATED FILE, do not edit.
 *
 * Regenerate: python ml/scripts/gen_test_vectors.py
 *
 * Expected values come from the Python reference implementation (scipy filter
 * design, cardia.preprocess), so the C code is checked against an independent
 * implementation rather than against itself.
 */

#ifndef CARDIA_TEST_VECTORS_H
#define CARDIA_TEST_VECTORS_H

#include <stdint.h>

"""]

    # --- filter impulse responses -----------------------------------------
    impulse = np.zeros(N_IMPULSE)
    impulse[0] = 1.0
    cond = sp_signal.sosfilt(preprocess.bandpass_sos(), impulse)
    qrs = sp_signal.sosfilt(preprocess.qrs_bandpass_sos(), impulse)
    parts.append(f"#define TV_IMPULSE_LEN {N_IMPULSE}\n\n")
    parts.append(c_float_array("tv_bandpass_impulse", cond))
    parts.append(c_float_array("tv_qrs_bandpass_impulse", qrs))

    # --- per-beat normalisation -------------------------------------------
    rng = np.random.default_rng(11)
    raw_beat = (rng.standard_normal(cfg.BEAT_LEN) * 3.7 + 1.9).astype(np.float32)
    norm_beat = preprocess.normalize_beat(raw_beat)
    parts.append(f"#define TV_BEAT_LEN {cfg.BEAT_LEN}\n\n")
    parts.append(c_float_array("tv_norm_input", raw_beat))
    parts.append(c_float_array("tv_norm_expected", norm_beat))

    # --- RR features -------------------------------------------------------
    # Build a short R-peak series with a clear premature beat in it, run the
    # batch reference, and record what beat index 5 should produce.
    r = np.array([0, 360, 720, 1080, 1440, 1700, 2160, 2520, 2880], dtype=np.int64)
    feats = preprocess.rr_features(r)
    i = 5  # the premature beat
    pre = (r[i] - r[i - 1]) / cfg.FS_HZ
    post = (r[i + 1] - r[i]) / cfg.FS_HZ
    rr_all = np.diff(r) / cfg.FS_HZ
    lo, hi = max(0, i - 1 - cfg.RR_LOCAL_WINDOW), i - 1
    local = rr_all[lo:hi].mean() if hi > lo else pre
    parts.append(f"#define TV_RR_PRE   {pre:.9e}f\n")
    parts.append(f"#define TV_RR_POST  {post:.9e}f\n")
    parts.append(f"#define TV_RR_LOCAL {local:.9e}f\n\n")
    parts.append(c_float_array("tv_rr_expected", feats[i]))

    # --- synthetic ECG for the QRS detector --------------------------------
    sig, rpos = synthetic_ecg(N_SYNTH, cfg.FS_HZ)
    parts.append(f"#define TV_SYNTH_LEN {N_SYNTH}\n")
    parts.append(f"#define TV_SYNTH_NBEATS {len(rpos)}\n\n")
    parts.append(c_float_array("tv_synth_ecg", sig))
    parts.append(c_u32_array("tv_synth_r_peaks", rpos))

    parts.append("#endif /* CARDIA_TEST_VECTORS_H */\n")
    OUT.write_text("".join(parts))
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")
    print(f"  synthetic ECG: {N_SYNTH} samples, {len(rpos)} beats")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
