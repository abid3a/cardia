"""Hand-rolled int8 quantisation-aware training and export.

Why not torch.ao.quantization
-----------------------------
Because the number that matters at the end of this project is "does the C
integer pipeline produce the *same* class as the Python model, on every beat".
Answering that with a tolerance ("close enough") is worth much less than
answering it with an equality. To get equality, the Python side has to use the
identical arithmetic the C side uses: symmetric per-channel int8 weights,
affine per-tensor int8 activations, and a requantisation step implemented as a
saturating doubling high multiply followed by a rounding shift.

Writing ~150 lines of fake-quantisation is cheaper than bending a general
framework into exactly that shape, and it leaves nothing about the numerics
implicit.

The scheme (the standard TFLite/CMSIS-NN one)
--------------------------------------------
A real value r maps to an int8 q by  r = S * (q - Z)  where S is a float scale
and Z an integer zero-point.

* Weights are **symmetric** (Z = 0) and **per output channel** for convolutions.
  Symmetric because it deletes a whole cross-term from the accumulation:
  expanding sum (w_q - Z_w)(x_q - Z_x) with Z_w = 0 leaves one term involving
  the input offset instead of four. Per-channel because different filters learn
  wildly different weight magnitudes, and forcing them onto one scale wastes
  most of the int8 range on the loudest filter.
* Activations are **affine** (Z != 0) and **per tensor**. After a ReLU the true
  range is [0, max], so an affine mapping with Z = -128 uses all 256 codes,
  where a symmetric mapping would throw away the entire negative half.
* The accumulator is int32. Bias is pre-divided into accumulator units
  (b_q = b / (S_x * S_w)) so it can simply be added.
* Requantisation from int32 accumulator to int8 output multiplies by
  M = (S_x * S_w) / S_out, expressed as an int32 multiplier and a shift.

Why QAT and not post-training quantisation
------------------------------------------
PTQ picks scales after the fact and hopes the rounding error is small. For a
network this small it is not: with only 8 filters in the first layer, rounding
one filter's weights degrades a full eighth of the representation, and there is
no redundancy left to absorb it. QAT instead puts the rounding *inside* the
training loop -- the forward pass sees quantised values, so the loss knows
about the error and the weights move to somewhere that survives rounding.
Gradients pass through the non-differentiable round() with a straight-through
estimator (identity inside the clipping range, zero outside).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import torch
import torch.nn as nn
import torch.nn.functional as F

from . import config as cfg, model as M

QMIN, QMAX = -128, 127


# ---------------------------------------------------------------------------
# Straight-through fake quantisation
# ---------------------------------------------------------------------------
class _RoundSTE(torch.autograd.Function):
    """round() forward, identity backward."""

    @staticmethod
    def forward(ctx, x):
        return torch.round(x)

    @staticmethod
    def backward(ctx, g):
        return g


def _round_ste(x: torch.Tensor) -> torch.Tensor:
    return _RoundSTE.apply(x)


def fake_quant(x: torch.Tensor, scale: torch.Tensor, zero_point: torch.Tensor) -> torch.Tensor:
    """Quantise then immediately dequantise, keeping gradients flowing."""
    q = _round_ste(x / scale) + zero_point
    q = torch.clamp(q, QMIN, QMAX)
    return (q - zero_point) * scale


class ActObserver(nn.Module):
    """Exponential moving average of a tensor's min and max.

    An EMA rather than a running absolute min/max because a single noisy beat
    with a huge artefact would otherwise permanently stretch the scale and cost
    resolution on every subsequent beat. Momentum 0.99 tracks the distribution
    while ignoring one-off outliers.
    """

    def __init__(self, momentum: float = 0.99, symmetric: bool = False) -> None:
        super().__init__()
        self.momentum = momentum
        self.symmetric = symmetric
        self.register_buffer("min_val", torch.tensor(float("inf")))
        self.register_buffer("max_val", torch.tensor(float("-inf")))
        self.register_buffer("initialised", torch.tensor(0, dtype=torch.uint8))
        self.enabled = True

    @torch.no_grad()
    def observe(self, x: torch.Tensor) -> None:
        # Only during training: the scales must be frozen for evaluation and
        # export, or the exported model would not be the evaluated model.
        if not self.enabled or not self.training:
            return
        lo, hi = x.min().detach(), x.max().detach()
        if self.initialised.item() == 0:
            self.min_val.fill_(lo.item())
            self.max_val.fill_(hi.item())
            self.initialised.fill_(1)
        else:
            m = self.momentum
            self.min_val.mul_(m).add_((1 - m) * lo)
            self.max_val.mul_(m).add_((1 - m) * hi)

    def qparams(self) -> tuple[float, int]:
        lo = float(self.min_val.item())
        hi = float(self.max_val.item())
        if not math.isfinite(lo) or not math.isfinite(hi):
            return 1.0, 0
        # The quantised range must contain zero exactly, otherwise padding with
        # "zero" and a ReLU's floor land on different values.
        lo = min(lo, 0.0)
        hi = max(hi, 0.0)
        if self.symmetric:
            a = max(abs(lo), abs(hi), 1e-8)
            return a / 127.0, 0
        if hi - lo < 1e-8:
            return 1e-8, 0
        scale = (hi - lo) / (QMAX - QMIN)
        zp = int(round(QMIN - lo / scale))
        zp = max(QMIN, min(QMAX, zp))
        return scale, zp

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        self.observe(x)
        s, z = self.qparams()
        return fake_quant(
            x,
            torch.tensor(s, device=x.device, dtype=x.dtype),
            torch.tensor(float(z), device=x.device, dtype=x.dtype),
        )


def weight_scales(w: torch.Tensor, per_channel: bool) -> torch.Tensor:
    """Symmetric scales: max |w| over each output channel (or the whole tensor)
    divided by 127."""
    if per_channel:
        flat = w.reshape(w.shape[0], -1)
        amax = flat.abs().amax(dim=1).clamp(min=1e-8)
    else:
        amax = w.abs().amax().clamp(min=1e-8).reshape(1)
    return amax / 127.0


def fake_quant_weight(w: torch.Tensor, per_channel: bool) -> torch.Tensor:
    s = weight_scales(w, per_channel)
    shape = [-1] + [1] * (w.dim() - 1) if per_channel else [1] * w.dim()
    s = s.reshape(shape)
    q = torch.clamp(_round_ste(w / s), QMIN, QMAX)
    return q * s


# ---------------------------------------------------------------------------
# QAT model
# ---------------------------------------------------------------------------
class CardiaNetQAT(nn.Module):
    """`CardiaNetFolded` with fake quantisation inserted at every point the C
    implementation stores an int8 tensor.

    The observer placement is not decorative -- each one corresponds to a real
    buffer in `firmware/src/nn/inference.c`. If a fake-quant node exists here
    that has no counterpart there, the two implementations will disagree.
    """

    def __init__(self, folded: M.CardiaNetFolded) -> None:
        super().__init__()
        self.net = folded
        self.obs_in = ActObserver(symmetric=False)     # normalised beat window
        self.obs_c1 = ActObserver()                    # after conv1 + ReLU
        self.obs_c2 = ActObserver()                    # after conv2 + ReLU
        self.obs_c3 = ActObserver()                    # after conv3 + ReLU
        self.obs_fused = ActObserver()                 # GAP output concat RR
        self.obs_fc1 = ActObserver()                   # after fc1 + ReLU
        self.quant_enabled = True

    def _fq(self, obs: ActObserver, x: torch.Tensor) -> torch.Tensor:
        return obs(x) if self.quant_enabled else x

    def _fqw(self, w: torch.Tensor, per_channel: bool) -> torch.Tensor:
        return fake_quant_weight(w, per_channel) if self.quant_enabled else w

    def forward(self, beat: torch.Tensor, rr: torch.Tensor) -> torch.Tensor:
        n = self.net
        x = self._fq(self.obs_in, beat).unsqueeze(1)

        x = F.conv1d(x, self._fqw(n.conv1.weight, True), n.conv1.bias,
                     stride=M.C1_S, padding=M.C1_P)
        x = self._fq(self.obs_c1, F.relu(x))
        x = F.max_pool1d(x, 2)

        x = F.conv1d(x, self._fqw(n.conv2.weight, True), n.conv2.bias,
                     stride=M.C2_S, padding=M.C2_P)
        x = self._fq(self.obs_c2, F.relu(x))
        x = F.max_pool1d(x, 2)

        x = F.conv1d(x, self._fqw(n.conv3.weight, True), n.conv3.bias,
                     stride=M.C3_S, padding=M.C3_P)
        x = self._fq(self.obs_c3, F.relu(x))

        x = x.mean(dim=2)
        # Both branches must share one scale to be concatenated as int8, so a
        # single observer watches the concatenated vector and both sides are
        # requantised into it.
        x = torch.cat([x, rr], dim=1)
        x = self._fq(self.obs_fused, x)

        x = F.linear(x, self._fqw(n.fc1.weight, False), n.fc1.bias)
        x = self._fq(self.obs_fc1, F.relu(x))

        # No observer on the logits: the firmware keeps the final layer's int32
        # accumulator and takes argmax directly. Requantising here would throw
        # away the very margin the decision depends on.
        return F.linear(x, self._fqw(n.fc2.weight, False), n.fc2.bias)


# ---------------------------------------------------------------------------
# Export
# ---------------------------------------------------------------------------
def quantize_multiplier(real_multiplier: float) -> tuple[int, int]:
    """Express a positive real scale factor as int32 mult * 2^(shift-31).

    The multiplier is normalised into [0.5, 1) and then scaled by 2^31, so it
    always uses the full 31 bits of precision regardless of magnitude. This is
    the same decomposition gemmlowp and CMSIS-NN use.
    """
    if real_multiplier <= 0.0:
        return 0, 0
    m, exp = math.frexp(real_multiplier)  # real = m * 2^exp, m in [0.5, 1)
    q = int(round(m * (1 << 31)))
    if q == (1 << 31):
        q //= 2
        exp += 1
    assert q <= 0x7FFFFFFF, q
    # real = (q / 2^31) * 2^exp = q * 2^(exp-31), which is exactly what
    # cardia_requantize(acc, q, exp) computes.
    return q, exp


@dataclass
class QuantLayer:
    name: str
    weight_q: torch.Tensor        # int8
    bias_q: torch.Tensor          # int32, in accumulator units
    weight_scales: torch.Tensor   # float, per channel or scalar
    mult: list[int] = field(default_factory=list)
    shift: list[int] = field(default_factory=list)
    input_scale: float = 1.0
    input_zp: int = 0
    output_scale: float = 1.0
    output_zp: int = 0


@dataclass
class QuantModel:
    layers: dict[str, QuantLayer]
    input_scale: float
    input_zp: int
    fused_scale: float
    fused_zp: int
    logit_scale: float


def _q_weight(w: torch.Tensor, per_channel: bool) -> tuple[torch.Tensor, torch.Tensor]:
    s = weight_scales(w, per_channel)
    shape = [-1] + [1] * (w.dim() - 1) if per_channel else [1] * w.dim()
    q = torch.clamp(torch.round(w / s.reshape(shape)), QMIN, QMAX).to(torch.int8)
    return q, s


def _mult_shift(input_scale: float, w_scales: torch.Tensor, output_scale: float):
    mults, shifts = [], []
    for ws in w_scales.tolist():
        m, sh = quantize_multiplier(input_scale * ws / output_scale)
        mults.append(m)
        shifts.append(sh)
    return mults, shifts


def export(qat: CardiaNetQAT) -> QuantModel:
    """Freeze the QAT model into integer tensors plus requantisation params."""
    n = qat.net
    in_s, in_z = qat.obs_in.qparams()
    c1_s, c1_z = qat.obs_c1.qparams()
    c2_s, c2_z = qat.obs_c2.qparams()
    c3_s, c3_z = qat.obs_c3.qparams()
    fu_s, fu_z = qat.obs_fused.qparams()
    f1_s, f1_z = qat.obs_fc1.qparams()

    layers: dict[str, QuantLayer] = {}

    def add_conv(name, conv, in_scale, in_zp, out_scale, out_zp):
        wq, ws = _q_weight(conv.weight.detach(), per_channel=True)
        bq = torch.round(conv.bias.detach() / (in_scale * ws)).to(torch.int32)
        mult, shift = _mult_shift(in_scale, ws, out_scale)
        layers[name] = QuantLayer(name, wq, bq, ws, mult, shift,
                                  in_scale, in_zp, out_scale, out_zp)

    add_conv("conv1", n.conv1, in_s, in_z, c1_s, c1_z)
    add_conv("conv2", n.conv2, c1_s, c1_z, c2_s, c2_z)
    add_conv("conv3", n.conv3, c2_s, c2_z, c3_s, c3_z)

    # Global average pool: acc = sum(x_q - Z_in) over L3 positions, so the real
    # output is S_in * acc / L3 and the requantisation multiplier folds the
    # division by L3 in, keeping exactly one rounding step.
    gap_mult, gap_shift = quantize_multiplier(c3_s / (M.L3 * fu_s))
    layers["gap"] = QuantLayer("gap", torch.empty(0, dtype=torch.int8),
                               torch.empty(0, dtype=torch.int32),
                               torch.empty(0),
                               [gap_mult], [gap_shift], c3_s, c3_z, fu_s, fu_z)

    wq, ws = _q_weight(n.fc1.weight.detach(), per_channel=False)
    bq = torch.round(n.fc1.bias.detach() / (fu_s * ws[0])).to(torch.int32)
    m, sh = quantize_multiplier(fu_s * float(ws[0]) / f1_s)
    layers["fc1"] = QuantLayer("fc1", wq, bq, ws, [m], [sh], fu_s, fu_z, f1_s, f1_z)

    # Final layer stays per-tensor on purpose: with per-channel weight scales
    # the five int32 accumulators would live in five different units and argmax
    # over them would be meaningless.
    wq2, ws2 = _q_weight(n.fc2.weight.detach(), per_channel=False)
    bq2 = torch.round(n.fc2.bias.detach() / (f1_s * ws2[0])).to(torch.int32)
    logit_scale = f1_s * float(ws2[0])
    layers["fc2"] = QuantLayer("fc2", wq2, bq2, ws2, [0], [0], f1_s, f1_z, logit_scale, 0)

    return QuantModel(layers=layers, input_scale=in_s, input_zp=in_z,
                      fused_scale=fu_s, fused_zp=fu_z, logit_scale=logit_scale)
