"""Training: float stage, then BatchNorm folding, then quantisation-aware
fine-tuning.

Model selection detail that is easy to get wrong and quietly invalidates
everything: **the validation set is also patient-disjoint.** DS1's 22 records
are split 17/5 by record, and the 5 held-out patients are used to choose the
epoch and nothing else. Selecting the best epoch on a random slice of DS1
beats would leak the same way a random train/test split does -- the chosen
checkpoint would be the one that best memorised DS1's patients, which is
exactly the failure mode the whole protocol exists to avoid. DS2 is never
touched until the final evaluation.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset

from . import config as cfg, dataset, metrics, model as M, quantize as Q

# Five DS1 records held out for model selection. Chosen so the validation set
# actually contains the minority classes -- 201/209/223 carry the S beats,
# 106/201/223 carry substantial V, and 223 carries F. A validation split with
# no S beats in it would give a macro-F1 that cannot see the hardest class.
DS1_VAL = (101, 106, 201, 209, 223)


@dataclass
class TrainConfig:
    float_epochs: int = 40
    qat_epochs: int = 20
    batch_size: int = 256
    lr: float = 2e-3
    qat_lr: float = 2e-4
    weight_decay: float = 1e-4
    class_weight_alpha: float = 0.5
    noise_std: float = 0.05
    drift_amp: float = 0.10
    seed: int = 1337


def set_seed(seed: int) -> None:
    torch.manual_seed(seed)
    np.random.seed(seed)


def class_weights(labels: np.ndarray, alpha: float, cap: float = 10.0) -> torch.Tensor:
    """Inverse-frequency weights, softened by an exponent and referenced to the
    median-frequency class.

        w_c = (median_count / count_c) ** alpha

    Raw inverse frequency (alpha = 1) would weight the 8 Q beats in DS1 about
    5700x a normal beat, and the model would burn its entire 5k-parameter
    capacity chasing eight examples. alpha = 0.5 (inverse square root) is the
    usual compromise: enough pressure that the minority classes are not simply
    ignored, not so much that positive predictive value collapses. This is a
    real trade-off dial between sensitivity and PPV, not a free win -- pushing
    alpha up raises S sensitivity and lowers S PPV, one for one.

    Normalising against the *median* count rather than the mean matters: the
    mean is dragged upward by the Q class's enormous raw weight, which would
    compress every other class toward the clipping floor and quietly undo the
    weighting entirely.
    """
    counts = np.array([(labels == i).sum() for i in range(cfg.N_CLASSES)], dtype=np.float64)
    counts = np.maximum(counts, 1.0)
    ref = float(np.median(counts))
    w = (ref / counts) ** alpha
    return torch.tensor(np.clip(w, 1.0 / cap, cap), dtype=torch.float32)


def augment(beats: torch.Tensor, noise_std: float, drift_amp: float) -> torch.Tensor:
    """Additive noise plus a random linear baseline tilt.

    Deliberately modest. Amplitude scaling would be a no-op because the windows
    are already per-beat z-scored, and time-warping a beat changes QRS width --
    which is the primary cue for the V class, so warping would be teaching the
    model something false. Residual baseline tilt is the realistic corruption:
    the 0.5 Hz high-pass suppresses wander but does not remove it.
    """
    b, n = beats.shape
    out = beats + noise_std * torch.randn_like(beats)
    if drift_amp > 0:
        ramp = torch.linspace(-1.0, 1.0, n, device=beats.device).unsqueeze(0)
        out = out + drift_amp * torch.randn(b, 1, device=beats.device) * ramp
    return out


def _loaders(train_bs: dataset.BeatSet, val_bs: dataset.BeatSet, cfgt: TrainConfig):
    tr = TensorDataset(
        torch.from_numpy(train_bs.beats), torch.from_numpy(train_bs.rr),
        torch.from_numpy(train_bs.labels),
    )
    va = TensorDataset(
        torch.from_numpy(val_bs.beats), torch.from_numpy(val_bs.rr),
        torch.from_numpy(val_bs.labels),
    )
    return (
        DataLoader(tr, batch_size=cfgt.batch_size, shuffle=True, drop_last=True),
        DataLoader(va, batch_size=1024, shuffle=False),
    )


@torch.no_grad()
def evaluate(net: nn.Module, loader: DataLoader) -> dict:
    net.eval()
    preds, trues = [], []
    for beat, rr, y in loader:
        logits = net(beat, rr)
        preds.append(logits.argmax(dim=1).numpy())
        trues.append(y.numpy())
    return metrics.summarize(np.concatenate(trues), np.concatenate(preds))


def _run_epochs(net, loader, val_loader, epochs, lr, wd, weights, cfgt, tag, log):
    opt = torch.optim.AdamW(net.parameters(), lr=lr, weight_decay=wd)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=max(epochs, 1))
    best_f1, best_state = -1.0, None

    for ep in range(epochs):
        net.train()
        total, nb = 0.0, 0
        for beat, rr, y in loader:
            beat = augment(beat, cfgt.noise_std, cfgt.drift_amp)
            opt.zero_grad()
            loss = F.cross_entropy(net(beat, rr), y, weight=weights)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 5.0)
            opt.step()
            total += float(loss.item())
            nb += 1
        sched.step()

        res = evaluate(net, val_loader)
        f1 = res["macro_f1"]
        line = (f"[{tag}] epoch {ep+1:3d}/{epochs}  loss {total/max(nb,1):.4f}  "
                f"val acc {res['accuracy']*100:5.2f}%  macro-F1 {f1*100:5.2f}%  "
                f"S-sens {res['per_class']['S']['sensitivity']*100:5.1f}%  "
                f"V-sens {res['per_class']['V']['sensitivity']*100:5.1f}%")
        print(line, flush=True)
        log.append(line)

        if f1 == f1 and f1 > best_f1:
            best_f1 = f1
            best_state = {k: v.detach().clone() for k, v in net.state_dict().items()}

    if best_state is not None:
        net.load_state_dict(best_state)
    return best_f1


def train_all(data_dir: Path = dataset.DEFAULT_DATA_DIR,
              cfgt: TrainConfig | None = None,
              out_dir: Path | None = None) -> dict:
    cfgt = cfgt or TrainConfig()
    out_dir = out_dir or (Path(__file__).resolve().parents[1] / "artifacts")
    out_dir.mkdir(parents=True, exist_ok=True)
    set_seed(cfgt.seed)
    log: list[str] = []

    ds1 = dataset.load_cached("ds1", data_dir)
    val_mask = np.isin(ds1.records, np.array(DS1_VAL))
    train_bs = dataset.BeatSet(ds1.beats[~val_mask], ds1.rr[~val_mask],
                               ds1.labels[~val_mask], ds1.records[~val_mask])
    val_bs = dataset.BeatSet(ds1.beats[val_mask], ds1.rr[val_mask],
                             ds1.labels[val_mask], ds1.records[val_mask])

    assert not (set(np.unique(train_bs.records).tolist()) &
                set(np.unique(val_bs.records).tolist())), "train/val patient leak"

    print(f"train {len(train_bs)} beats from {len(np.unique(train_bs.records))} patients "
          f"{train_bs.counts()}")
    print(f"val   {len(val_bs)} beats from {len(np.unique(val_bs.records))} patients "
          f"{val_bs.counts()}")

    loader, val_loader = _loaders(train_bs, val_bs, cfgt)
    weights = class_weights(train_bs.labels, cfgt.class_weight_alpha)
    print("class weights:", {c: round(float(w), 3)
                             for c, w in zip(("N", "S", "V", "F", "Q"), weights)})

    # --- stage 1: float ----------------------------------------------------
    net = M.CardiaNet()
    f1_float = _run_epochs(net, loader, val_loader, cfgt.float_epochs, cfgt.lr,
                           cfgt.weight_decay, weights, cfgt, "float", log)

    # --- stage 2: fold BatchNorm ------------------------------------------
    folded = M.CardiaNetFolded.from_folded(M.fold_bn(net))
    res_folded = evaluate(folded, val_loader)
    print(f"[fold] val macro-F1 {res_folded['macro_f1']*100:.2f}% "
          f"(was {f1_float*100:.2f}%) -- folding is exact, any change here is a bug")

    # --- stage 3: QAT ------------------------------------------------------
    qat = Q.CardiaNetQAT(folded)
    # Each observer records the tensor's range before it computes qparams, so
    # the very first forward already quantises with sensible scales; no
    # separate calibration pass is needed. A few batches of drift while the
    # EMAs settle is harmless because the weights barely move at the QAT
    # learning rate.
    qat.train()
    with torch.no_grad():
        for i, (beat, rr, _) in enumerate(loader):
            qat(beat, rr)
            if i >= 20:
                break

    f1_qat = _run_epochs(qat, loader, val_loader, cfgt.qat_epochs, cfgt.qat_lr,
                         cfgt.weight_decay, weights, cfgt, "qat", log)

    torch.save({"float": net.state_dict(),
                "folded": folded.state_dict(),
                "qat": qat.state_dict()}, out_dir / "cardia.pt")

    summary = {
        "float_val_macro_f1": f1_float,
        "folded_val_macro_f1": res_folded["macro_f1"],
        "qat_val_macro_f1": f1_qat,
        "train_beats": len(train_bs),
        "val_beats": len(val_bs),
        "train_patients": sorted(np.unique(train_bs.records).tolist()),
        "val_patients": sorted(np.unique(val_bs.records).tolist()),
        "class_weights": [float(w) for w in weights],
        "macs": M.mac_count(),
        "params": M.param_count(),
        "config": cfgt.__dict__,
    }
    (out_dir / "train_summary.json").write_text(json.dumps(summary, indent=2))
    (out_dir / "train_log.txt").write_text("\n".join(log))
    return summary
