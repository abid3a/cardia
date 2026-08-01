"""Signal conditioning and beat extraction.

Every operation here is deliberately **causal**. It would be easy to get a
cleaner signal in Python with `scipy.signal.filtfilt` (zero-phase, forward then
backward), and a lot of published ECG code does exactly that -- but filtfilt
needs the whole record in memory and looks into the future, so it can never run
on an MCU processing a live sample stream. Anything the Python reference does
that the firmware cannot do is a lie that shows up later as a parity failure.
So: one-directional filtering, running statistics, no lookahead beyond the one
beat the RR features explicitly account for.
"""

from __future__ import annotations

import numpy as np
from scipy import signal as sp_signal

from . import config as cfg


# ---------------------------------------------------------------------------
# Filtering
# ---------------------------------------------------------------------------
def bandpass_sos(
    fs: int = cfg.FS_HZ,
    low_hz: float = cfg.BP_LOW_HZ,
    high_hz: float = cfg.BP_HIGH_HZ,
    order: int = cfg.BP_ORDER,
) -> np.ndarray:
    """Butterworth bandpass as second-order sections.

    Butterworth is chosen over Chebyshev/elliptic because it is maximally flat
    in the passband. ECG diagnosis is about morphology -- the relative heights
    and slopes of P, QRS and T -- so passband ripple would distort exactly the
    thing being measured. We pay for that flatness with a gentler roll-off,
    which is fine here: nothing important sits immediately outside 0.5-40 Hz.

    SOS (cascaded biquads) rather than a single high-order transfer function
    because direct-form implementation of a 4th-order IIR is numerically fragile
    in float32, and because the firmware runs the identical cascade structure.
    """
    nyq = fs / 2.0
    return sp_signal.butter(
        order // 2,  # per-section order; butter(N, ..., 'bandpass') yields 2N poles
        [low_hz / nyq, high_hz / nyq],
        btype="bandpass",
        output="sos",
    )


def qrs_bandpass_sos(fs: int = cfg.FS_HZ) -> np.ndarray:
    """5-15 Hz band used only by the QRS detector. See config.py for why the
    detector gets its own filter rather than reusing the 0.5-40 Hz one."""
    nyq = fs / 2.0
    return sp_signal.butter(
        cfg.QRS_BP_ORDER // 2,
        [cfg.QRS_BP_LOW_HZ / nyq, cfg.QRS_BP_HIGH_HZ / nyq],
        btype="bandpass",
        output="sos",
    )


def bandpass_filter(x: np.ndarray, sos: np.ndarray | None = None) -> np.ndarray:
    """Causal bandpass. Deliberately `sosfilt`, never `sosfiltfilt`."""
    if sos is None:
        sos = bandpass_sos()
    return sp_signal.sosfilt(sos, x.astype(np.float64)).astype(np.float32)


# ---------------------------------------------------------------------------
# Beat windowing
# ---------------------------------------------------------------------------
def extract_beat(
    x: np.ndarray,
    r_index: int,
    pre: int = cfg.BEAT_PRE,
    post: int = cfg.BEAT_POST,
) -> np.ndarray | None:
    """Cut a fixed window around an R peak. Returns None if it would run off
    either end of the record -- padding would invent signal that the firmware
    would never see, so those (few) edge beats are dropped in both tracks."""
    start = r_index - pre
    stop = r_index + post
    if start < 0 or stop > len(x):
        return None
    return x[start:stop]


def normalize_beat(beat: np.ndarray, eps: float = 1e-6) -> np.ndarray:
    """Per-beat z-score.

    Why per-beat and not per-record or global: electrode placement, skin
    impedance, body habitus and amplifier gain vary enormously between
    patients, and the whole point of the inter-patient protocol is that the
    test patients are strangers. A global scale learned on DS1 would simply not
    apply to DS2. Standardising each window makes the model depend on *shape*,
    which does transfer.

    The cost, stated honestly: absolute QRS amplitude is discarded, and large
    amplitude is one cue for ventricular beats. QRS *width* and the RR features
    still carry that information, and width is the stronger cue anyway.
    On the MCU this is two passes over 256 samples plus one reciprocal-sqrt --
    a few microseconds, and no calibration table to ship.
    """
    mean = beat.mean()
    std = beat.std()
    return ((beat - mean) / (std + eps)).astype(np.float32)


# ---------------------------------------------------------------------------
# RR-interval features
# ---------------------------------------------------------------------------
def rr_features(
    r_indices: np.ndarray,
    fs: int = cfg.FS_HZ,
    local_window: int = cfg.RR_LOCAL_WINDOW,
    clip: float = cfg.RR_RATIO_CLIP,
) -> np.ndarray:
    """Four timing features per beat, shape (n_beats, 4).

    f0  pre-RR   (s)  interval from the previous R peak to this one
    f1  post-RR  (s)  interval from this R peak to the next one
    f2  pre-RR / local mean of the previous `local_window` RR intervals
    f3  post-RR / pre-RR

    f0/f2 detect prematurity -- the signature of a supraventricular ectopic
    beat, which is morphologically almost identical to a normal beat. f1/f3
    detect the compensatory pause: a PVC is usually followed by a full
    compensatory pause (post-RR clearly longer than pre-RR), an atrial
    premature beat usually resets the sinus node and is followed by a shorter,
    non-compensatory pause. That distinction is the classic clinical rule for
    telling S from V, and it is pure timing.

    f1 and f3 need the *next* R peak, so a beat can only be classified one beat
    after it occurs. That one-beat latency (~0.8 s at rest) is a real design
    cost, accepted deliberately and implemented the same way in the firmware,
    which keeps a one-beat delay queue.

    The local mean in f2 is causal -- previous intervals only, never the whole
    record's average. Using a record-wide average (as some published work does)
    would leak future information into every beat.
    """
    r_indices = np.asarray(r_indices, dtype=np.int64)
    n = len(r_indices)
    feats = np.zeros((n, cfg.N_RR_FEATURES), dtype=np.float32)
    if n < 2:
        return feats

    rr = np.diff(r_indices) / float(fs)  # rr[i] = interval between beat i and i+1

    for i in range(n):
        pre = rr[i - 1] if i >= 1 else rr[0]
        post = rr[i] if i < n - 1 else rr[-1]

        lo = max(0, i - 1 - local_window)
        hi = i - 1
        local = rr[lo:hi].mean() if hi > lo else pre

        feats[i, 0] = pre
        feats[i, 1] = post
        feats[i, 2] = np.clip(pre / (local + 1e-6), -clip, clip)
        feats[i, 3] = np.clip(post / (pre + 1e-6), -clip, clip)

    # Keep every feature in roughly [-1, 1] so a single int8 input scale covers
    # all four without wasting quantisation levels on one dominant channel.
    feats[:, 0] = np.clip(feats[:, 0] / cfg.RR_RATIO_CLIP, -1.0, 1.0)
    feats[:, 1] = np.clip(feats[:, 1] / cfg.RR_RATIO_CLIP, -1.0, 1.0)
    feats[:, 2] = feats[:, 2] / cfg.RR_RATIO_CLIP
    feats[:, 3] = feats[:, 3] / cfg.RR_RATIO_CLIP
    return feats
