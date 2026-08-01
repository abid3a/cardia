"""The classifier: a small 1-D CNN over beat morphology, fused with a tiny MLP
over RR-interval timing.

Why two branches
----------------
A purely morphological model cannot solve this problem, and the reason is
physiological rather than statistical. A supraventricular ectopic beat (class S)
originates above the ventricles, so it is conducted down the normal
His-Purkinje system and produces a QRS complex that looks essentially normal.
What makes it ectopic is that it arrives *early*. Feed only the waveform and the
network is being asked to distinguish two classes that genuinely look the same;
S sensitivity collapses into the teens. Feed it the RR intervals and the
defining feature is right there.

The mirror-image argument holds for V: a ventricular beat is conducted
cell-to-cell through myocardium instead of down the fast conduction system, so
it is wide and bizarre. Morphology carries that one. Hence two branches.

Why this size
-------------
The budget was set by the target before the architecture was chosen:
STM32F446RE at 180 MHz, and a beat arrives roughly every 800 ms at rest but as
often as every 300 ms in tachycardia. Sizing for ~100k MACs per beat leaves the
inference at well under 1% CPU, which keeps the entire margin available for
the sample-rate ISR and the DSP chain. The result is ~4.9k parameters --
about 5 KB of flash as int8.
"""

from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F

from . import config as cfg

# Layer geometry. Kept as module constants because the C code generator emits
# buffer sizes from the same numbers.
C1_OUT, C1_K, C1_S, C1_P = 8, 11, 4, 5
C2_OUT, C2_K, C2_S, C2_P = 16, 7, 1, 3
C3_OUT, C3_K, C3_S, C3_P = 32, 5, 1, 2
FC1_OUT = 32
# Width of the RR-interval branch.
#
# Earlier versions fed the four raw RR features straight into the fused vector
# alongside 32 pooled morphology channels. That looked economical and was
# measurably wrong: on the held-out validation patients a SINGLE hand-written
# threshold on the prematurity feature reached 82% S sensitivity at 56%
# positive predictivity, while the network managed 21% at 56%. The information
# was present and the model was not using it -- four inputs out of thirty-six,
# on a scale set by whatever the convolution stack happened to output, cannot
# compete for the first layer's attention. Giving the timing features their own
# projection lets them arrive at a comparable magnitude and with enough
# capacity to encode a decision boundary, for 64 extra MACs.
RR_HIDDEN = 16

L1 = (cfg.BEAT_LEN + 2 * C1_P - C1_K) // C1_S + 1   # 64
L1P = L1 // 2                                        # 32
L2 = (L1P + 2 * C2_P - C2_K) // C2_S + 1             # 32
L2P = L2 // 2                                        # 16
L3 = (L2P + 2 * C3_P - C3_K) // C3_S + 1             # 16

FUSED_IN = C3_OUT + RR_HIDDEN                        # 48
DROPOUT_P = 0.3


def mac_count() -> dict[str, int]:
    """Multiply-accumulates per beat, per layer. Used in docs/RESULTS.md and
    checked against the real-time budget."""
    m = {
        "conv1": L1 * C1_OUT * C1_K * 1,
        "conv2": L2 * C2_OUT * C2_K * C1_OUT,
        "conv3": L3 * C3_OUT * C3_K * C2_OUT,
        "rr_fc": cfg.N_RR_FEATURES * RR_HIDDEN,
        "fc1": FUSED_IN * FC1_OUT,
        "fc2": FC1_OUT * cfg.N_CLASSES,
    }
    m["total"] = sum(m.values())
    return m


def param_count() -> dict[str, int]:
    w = {
        "conv1": C1_OUT * 1 * C1_K,
        "conv2": C2_OUT * C1_OUT * C2_K,
        "conv3": C3_OUT * C2_OUT * C3_K,
        "rr_fc": cfg.N_RR_FEATURES * RR_HIDDEN,
        "fc1": FUSED_IN * FC1_OUT,
        "fc2": FC1_OUT * cfg.N_CLASSES,
    }
    b = {"conv1": C1_OUT, "conv2": C2_OUT, "conv3": C3_OUT, "rr_fc": RR_HIDDEN,
         "fc1": FC1_OUT, "fc2": cfg.N_CLASSES}
    out = {k: w[k] + b[k] for k in w}
    out["weights_total"] = sum(w.values())
    out["biases_total"] = sum(b.values())
    out["total"] = sum(out[k] for k in w)
    return out


class CardiaNet(nn.Module):
    """Float model with BatchNorm, used for the first training stage.

    BatchNorm is present only during float training: it makes this depth
    trainable with a plain Adam schedule and no warmup fiddling. It is folded
    into the preceding convolution's weights before quantisation, so it costs
    the MCU nothing -- an inference-time BN is just a per-channel affine map,
    which is exactly what a conv weight and bias already are.
    """

    def __init__(self) -> None:
        super().__init__()
        self.conv1 = nn.Conv1d(1, C1_OUT, C1_K, stride=C1_S, padding=C1_P, bias=False)
        self.bn1 = nn.BatchNorm1d(C1_OUT)
        self.conv2 = nn.Conv1d(C1_OUT, C2_OUT, C2_K, stride=C2_S, padding=C2_P, bias=False)
        self.bn2 = nn.BatchNorm1d(C2_OUT)
        self.conv3 = nn.Conv1d(C2_OUT, C3_OUT, C3_K, stride=C3_S, padding=C3_P, bias=False)
        self.bn3 = nn.BatchNorm1d(C3_OUT)
        self.rr_fc = nn.Linear(cfg.N_RR_FEATURES, RR_HIDDEN)
        self.fc1 = nn.Linear(FUSED_IN, FC1_OUT)
        self.fc2 = nn.Linear(FC1_OUT, cfg.N_CLASSES)
        # Dropout only on the fused feature vector, and only during training.
        # Inter-patient generalisation is this project's whole difficulty: the
        # model fits DS1's 17 patients to a training loss of ~0.02 while
        # validation accuracy swings by 20 points between epochs, which is
        # textbook memorisation of individuals. At inference dropout is the
        # identity, so it costs the MCU nothing and does not appear in the
        # exported C at all.
        self.drop = nn.Dropout(DROPOUT_P)

    def forward(self, beat: torch.Tensor, rr: torch.Tensor) -> torch.Tensor:
        x = beat.unsqueeze(1)                      # (B, 1, 256)
        x = F.relu(self.bn1(self.conv1(x)))        # (B, 8, 64)
        x = F.max_pool1d(x, 2)                     # (B, 8, 32)
        x = F.relu(self.bn2(self.conv2(x)))        # (B, 16, 32)
        x = F.max_pool1d(x, 2)                     # (B, 16, 16)
        x = F.relu(self.bn3(self.conv3(x)))        # (B, 32, 16)
        x = x.mean(dim=2)                          # global average pool -> (B, 32)
        r = F.relu(self.rr_fc(rr))                 # timing branch -> (B, 16)
        x = torch.cat([x, r], dim=1)               # (B, 48)
        x = F.relu(self.fc1(self.drop(x)))
        return self.fc2(x)


def fold_bn(model: CardiaNet) -> dict[str, torch.Tensor]:
    """Fold each BatchNorm into the convolution before it.

    For a conv with weight W and no bias followed by BN(gamma, beta, mu, var):
        y = gamma * (W*x - mu) / sqrt(var + eps) + beta
          = (gamma/sqrt(var+eps) * W) * x  +  (beta - gamma*mu/sqrt(var+eps))
    which is a conv with scaled weights and a new bias. Exact, not an
    approximation -- at inference BN is a fixed per-channel affine transform.
    """
    out: dict[str, torch.Tensor] = {}
    model = model.eval()
    for ci, bi in ((1, 1), (2, 2), (3, 3)):
        conv: nn.Conv1d = getattr(model, f"conv{ci}")
        bn: nn.BatchNorm1d = getattr(model, f"bn{bi}")
        scale = bn.weight / torch.sqrt(bn.running_var + bn.eps)
        out[f"conv{ci}.weight"] = (conv.weight * scale.reshape(-1, 1, 1)).detach()
        out[f"conv{ci}.bias"] = (bn.bias - scale * bn.running_mean).detach()
    out["rr_fc.weight"] = model.rr_fc.weight.detach().clone()
    out["rr_fc.bias"] = model.rr_fc.bias.detach().clone()
    out["fc1.weight"] = model.fc1.weight.detach().clone()
    out["fc1.bias"] = model.fc1.bias.detach().clone()
    out["fc2.weight"] = model.fc2.weight.detach().clone()
    out["fc2.bias"] = model.fc2.bias.detach().clone()
    return out


class CardiaNetFolded(nn.Module):
    """The same network with BatchNorm already folded away. This is the exact
    graph the firmware executes, so it is also the graph QAT operates on."""

    def __init__(self) -> None:
        super().__init__()
        self.conv1 = nn.Conv1d(1, C1_OUT, C1_K, stride=C1_S, padding=C1_P)
        self.conv2 = nn.Conv1d(C1_OUT, C2_OUT, C2_K, stride=C2_S, padding=C2_P)
        self.conv3 = nn.Conv1d(C2_OUT, C3_OUT, C3_K, stride=C3_S, padding=C3_P)
        self.rr_fc = nn.Linear(cfg.N_RR_FEATURES, RR_HIDDEN)
        self.fc1 = nn.Linear(FUSED_IN, FC1_OUT)
        self.fc2 = nn.Linear(FC1_OUT, cfg.N_CLASSES)
        self.drop = nn.Dropout(DROPOUT_P)

    @classmethod
    def from_folded(cls, folded: dict[str, torch.Tensor]) -> "CardiaNetFolded":
        m = cls()
        with torch.no_grad():
            m.conv1.weight.copy_(folded["conv1.weight"])
            m.conv1.bias.copy_(folded["conv1.bias"])
            m.conv2.weight.copy_(folded["conv2.weight"])
            m.conv2.bias.copy_(folded["conv2.bias"])
            m.conv3.weight.copy_(folded["conv3.weight"])
            m.conv3.bias.copy_(folded["conv3.bias"])
            m.rr_fc.weight.copy_(folded["rr_fc.weight"])
            m.rr_fc.bias.copy_(folded["rr_fc.bias"])
            m.fc1.weight.copy_(folded["fc1.weight"])
            m.fc1.bias.copy_(folded["fc1.bias"])
            m.fc2.weight.copy_(folded["fc2.weight"])
            m.fc2.bias.copy_(folded["fc2.bias"])
        return m

    def forward(self, beat: torch.Tensor, rr: torch.Tensor) -> torch.Tensor:
        x = beat.unsqueeze(1)
        x = F.relu(self.conv1(x))
        x = F.max_pool1d(x, 2)
        x = F.relu(self.conv2(x))
        x = F.max_pool1d(x, 2)
        x = F.relu(self.conv3(x))
        x = x.mean(dim=2)
        r = F.relu(self.rr_fc(rr))
        x = torch.cat([x, r], dim=1)
        x = F.relu(self.fc1(self.drop(x)))
        return self.fc2(x)
