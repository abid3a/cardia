"""Pure-integer numpy reference implementation of the quantised network.

This exists to make one specific claim testable: **the firmware computes the
same thing the model does.**

There are three implementations of this network in the project, and it matters
that they are three and not one:

  1. `quantize.CardiaNetQAT` -- float with fake quantisation. What training
     optimises.
  2. this module -- integer arithmetic in numpy. Independent of both PyTorch
     and C.
  3. `firmware/src/nn/inference.c` -- integer arithmetic in C. What actually
     runs.

(2) and (3) are written from the same specification but share no code, so
agreement between them is evidence, not tautology. The parity test in `sim/`
requires them to agree on **every** beat, bit for bit -- not within a
tolerance. (1) vs (2) is expected to differ on a small fraction of beats,
because fake quantisation accumulates in float while the integer path
accumulates in int32 and rounds once at the end; that disagreement rate is
measured and reported rather than assumed to be zero.
"""

from __future__ import annotations

import numpy as np

from . import config as cfg, model as M

QMIN, QMAX = -128, 127


# ---------------------------------------------------------------------------
# Fixed-point primitives -- must mirror firmware/src/nn/nn_kernels.h exactly
# ---------------------------------------------------------------------------
def sat_doubling_high_mul(a: np.ndarray, b) -> np.ndarray:
    """(a * b) >> 31 with round-half-away-from-zero. `b` may be an array so a
    whole layer's per-channel multipliers apply in one pass."""
    a = np.asarray(a, dtype=np.int64)
    prod = a * np.asarray(b, dtype=np.int64)
    nudge = np.where(prod >= 0, np.int64(1 << 30), np.int64(1 - (1 << 30)))
    num = prod + nudge
    den = np.int64(1) << 31
    # C integer division truncates toward zero; numpy // floors. Emulate C.
    return (np.sign(num) * (np.abs(num) // den)).astype(np.int64)


def rounding_div_pot(x: np.ndarray, exponent) -> np.ndarray:
    """Rounding right shift. `exponent` may be an array (per channel); an
    exponent of 0 is the identity, which falls out of the same expression."""
    x = np.asarray(x, dtype=np.int64)
    exponent = np.asarray(exponent, dtype=np.int64)
    mask = (np.int64(1) << exponent) - 1
    remainder = x & mask
    threshold = (mask >> 1) + (x < 0).astype(np.int64)
    return (x >> exponent) + (remainder > threshold).astype(np.int64)


def requantize(acc: np.ndarray, mult, shift) -> np.ndarray:
    shift = np.asarray(shift, dtype=np.int64)
    left = np.where(shift > 0, shift, 0)
    right = np.where(shift > 0, 0, -shift)
    scaled = np.asarray(acc, dtype=np.int64) * (np.int64(1) << left)
    # The C code holds this in int32 before the multiply; wrap identically so a
    # value that would overflow on the MCU also overflows here rather than
    # quietly giving a better answer than the firmware can.
    scaled = scaled.astype(np.int32).astype(np.int64)
    return rounding_div_pot(sat_doubling_high_mul(scaled, mult), right)


def sat_int8(v: np.ndarray) -> np.ndarray:
    return np.clip(v, QMIN, QMAX).astype(np.int8)


def quantize_f32(x: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    v = x.astype(np.float32) / np.float32(scale)
    q = np.where(v >= 0, np.floor(v + 0.5), np.ceil(v - 0.5)).astype(np.int64) + zero_point
    return sat_int8(q)


# ---------------------------------------------------------------------------
# Layers. Tensor layout is (length, channels) -- channels last, as in C.
# ---------------------------------------------------------------------------
def conv1d_s8(x, w, b, mult, shift, stride, pad, in_zp, out_zp,
              act_min=QMIN, act_max=QMAX):
    """x: (L, Cin) int8. w: (Cout, K, Cin) int8. Returns (Lout, Cout) int8.

    Implemented as im2col + one matmul rather than nested loops. That is purely
    for speed on the host -- the C version keeps the explicit loops, because on
    a Cortex-M4 the im2col buffer would cost more RAM than the whole rest of
    the network. The arithmetic is identical either way, which is the point.
    """
    in_len, in_ch = x.shape
    out_ch, kernel, _ = w.shape
    out_len = (in_len + 2 * pad - kernel) // stride + 1

    # Applying the input offset first means the padding is plain zeros: a
    # padded element must contribute nothing to the sum, and after the offset
    # the quantised zero IS zero.
    xi = x.astype(np.int64) + np.int64(in_zp)
    if pad:
        xi = np.pad(xi, ((pad, pad), (0, 0)))

    idx = (np.arange(out_len) * stride)[:, None] + np.arange(kernel)[None, :]
    cols = xi[idx].reshape(out_len, kernel * in_ch)          # (Lout, K*Cin)
    wf = w.astype(np.int64).reshape(out_ch, kernel * in_ch)  # (Cout, K*Cin)

    acc = cols @ wf.T + b.astype(np.int64)[None, :]          # (Lout, Cout)
    acc32 = acc.astype(np.int32)

    v = requantize(acc32, np.asarray(mult, dtype=np.int64)[None, :],
                   np.asarray(shift, dtype=np.int64)[None, :]) + out_zp
    return sat_int8(np.clip(v, act_min, act_max))


def maxpool1d_s8(x, kernel):
    in_len, ch = x.shape
    out_len = in_len // kernel
    return x[: out_len * kernel].reshape(out_len, kernel, ch).max(axis=1).astype(np.int8)


def global_avgpool_s8(x, in_zp, out_zp, mult, shift):
    acc = (x.astype(np.int32) + np.int32(in_zp)).sum(axis=0).astype(np.int32)
    v = requantize(acc, mult, shift) + out_zp
    return sat_int8(v)


def fully_connected_s8(x, w, b, mult, shift, in_zp, out_zp,
                       act_min=QMIN, act_max=QMAX):
    xi = x.astype(np.int64) + np.int64(in_zp)
    acc = (w.astype(np.int64) @ xi) + b.astype(np.int64)
    acc32 = acc.astype(np.int32)
    v = requantize(acc32, mult, shift) + out_zp
    return sat_int8(np.clip(v, act_min, act_max))


def fully_connected_s32(x, w, b, in_zp):
    xi = x.astype(np.int64) + np.int64(in_zp)
    return ((w.astype(np.int64) @ xi) + b.astype(np.int64)).astype(np.int32)


# ---------------------------------------------------------------------------
# The whole network
# ---------------------------------------------------------------------------
class IntCardiaNet:
    """Integer forward pass over one beat, matching inference.c step for step."""

    def __init__(self, qm) -> None:
        self.qm = qm
        L = qm.layers
        # (Cout, Cin, K) from torch -> (Cout, K, Cin) channels-last for C.
        self.w1 = L["conv1"].weight_q.numpy().transpose(0, 2, 1).copy()
        self.w2 = L["conv2"].weight_q.numpy().transpose(0, 2, 1).copy()
        self.w3 = L["conv3"].weight_q.numpy().transpose(0, 2, 1).copy()
        self.b1 = L["conv1"].bias_q.numpy()
        self.b2 = L["conv2"].bias_q.numpy()
        self.b3 = L["conv3"].bias_q.numpy()
        self.wfc1 = L["fc1"].weight_q.numpy()
        self.bfc1 = L["fc1"].bias_q.numpy()
        self.wfc2 = L["fc2"].weight_q.numpy()
        self.bfc2 = L["fc2"].bias_q.numpy()

    def forward_logits(self, beat: np.ndarray, rr: np.ndarray) -> np.ndarray:
        qm, L = self.qm, self.qm.layers

        x = quantize_f32(beat, qm.input_scale, qm.input_zp).reshape(cfg.BEAT_LEN, 1)

        c1 = L["conv1"]
        # ReLU is folded into the requantisation clamp: the output zero-point is
        # the quantised representation of 0.0, so clamping the result at or
        # above it *is* a ReLU. One clamp instead of a separate pass.
        x = conv1d_s8(x, self.w1, self.b1, c1.mult, c1.shift, M.C1_S, M.C1_P,
                      -c1.input_zp, c1.output_zp, act_min=c1.output_zp)
        x = maxpool1d_s8(x, 2)

        c2 = L["conv2"]
        x = conv1d_s8(x, self.w2, self.b2, c2.mult, c2.shift, M.C2_S, M.C2_P,
                      -c2.input_zp, c2.output_zp, act_min=c2.output_zp)
        x = maxpool1d_s8(x, 2)

        c3 = L["conv3"]
        x = conv1d_s8(x, self.w3, self.b3, c3.mult, c3.shift, M.C3_S, M.C3_P,
                      -c3.input_zp, c3.output_zp, act_min=c3.output_zp)

        g = L["gap"]
        pooled = global_avgpool_s8(x, -g.input_zp, g.output_zp, g.mult[0], g.shift[0])

        rr_q = quantize_f32(np.asarray(rr, dtype=np.float32), qm.fused_scale, qm.fused_zp)
        fused = np.concatenate([pooled, rr_q]).astype(np.int8)

        f1 = L["fc1"]
        h = fully_connected_s8(fused, self.wfc1, self.bfc1, f1.mult[0], f1.shift[0],
                               -f1.input_zp, f1.output_zp, act_min=f1.output_zp)

        f2 = L["fc2"]
        return fully_connected_s32(h, self.wfc2, self.bfc2, -f2.input_zp)

    def predict(self, beat: np.ndarray, rr: np.ndarray) -> int:
        return int(np.argmax(self.forward_logits(beat, rr)))

    def predict_batch(self, beats: np.ndarray, rrs: np.ndarray) -> np.ndarray:
        return np.array([self.predict(b, r) for b, r in zip(beats, rrs)], dtype=np.int64)
