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

# Five DS1 records held out for model selection.
#
# Choosing these is a real constraint problem, and getting it wrong cost this
# project two full training runs. Three separate scarcities collide:
#
# 1. Minority CLASS concentration. Record 209 alone holds 383 of DS1's 944 S
#    beats; record 208 holds 372 of its 414 F beats. Move those to validation
#    and training has nothing to learn the class from.
#
# 2. Minority MORPHOLOGY concentration -- the one that actually bit. Only four
#    DS1 records contain bundle-branch block: 109 (LBBB), 118 (RBBB), 124
#    (RBBB), 207 (both). A bundle-branch-block beat is labelled N under AAMI,
#    but its QRS is wide and bizarre because conduction detours through
#    myocardium instead of the fast Purkinje system -- morphologically it looks
#    exactly like a ventricular beat. An earlier split put BOTH 118 and 124 in
#    validation, leaving training with 86 RBBB beats total. The model
#    reasonably concluded that wide QRS means V, and V positive predictivity
#    collapsed to 13%: it called roughly a third of all normal beats
#    ventricular. DS2 contains RBBB records (212, 231) and LBBB records (111,
#    214), so this morphology is not optional to learn.
#
# 3. Validation still has to be able to MEASURE the failure it is guarding
#    against, so exactly one bundle-branch record stays on the validation side.
#
# The resulting set keeps 118 (RBBB), 109 and 207 (LBBB), 208 (fusion) and 209
# (supraventricular) in training, holds 124 out as the bundle-branch canary,
# and leaves validation with ~260 S and ~425 V beats to rank checkpoints on.
DS1_VAL = (116, 124, 201, 205, 220)

# Epoch selection uses macro-F1 over N/S/V. See metrics.summarize for why F is
# excluded from the selection criterion but not from the reported results.
SELECTION_METRIC = "macro_f1_nsv"


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


def effective_patients(labels: np.ndarray, records: np.ndarray,
                       cls: int, frac: float = 0.10) -> int:
    """How many patients meaningfully contribute to a class.

    A class's beat count is a misleading measure of how learnable it is. DS1
    contains 414 fusion beats, which sounds workable -- but 372 of them come
    from a single patient (record 208). A model trained on that learns "this is
    what patient 208's fusion beats look like", which transfers to nobody.
    Counting patients that hold at least `frac` of the class's beats exposes
    that: F scores 1, while S and V score 4 each.
    """
    mask = labels == cls
    total = int(mask.sum())
    if total == 0:
        return 0
    recs, counts = np.unique(records[mask], return_counts=True)
    return int((counts >= frac * total).sum())


def class_weights(labels: np.ndarray, records: np.ndarray, alpha: float,
                  cap: float = 6.0, min_patients: int = 3) -> torch.Tensor:
    """Inverse-frequency weights, capped, and gated on patient diversity.

        w_c = clip((count_majority / count_c) ** alpha, 1, cap)

    alpha = 0.5 (inverse square root) rather than 1.0: raw inverse frequency
    would weight DS1's handful of Q beats thousands of times a normal beat, and
    a 5k-parameter model would burn its whole capacity on them. This exponent
    is a real trade-off dial, not a free win -- raising it lifts S sensitivity
    and drops S positive predictivity roughly one for one.

    The patient-diversity gate is the part that was learned the hard way. An
    earlier version up-weighted every minority class purely on beat count. F
    got a weight of 1.35 on the strength of 397 training beats, 94% of which
    came from one patient, and the resulting model predicted F for 3049 normal
    beats in DS2 -- an F positive predictivity of 0.25%, which dragged overall
    accuracy down by roughly six points. A class that lives in one patient
    cannot be generalised from, and boosting it only teaches the model to fire
    on that patient's idiosyncratic noise. So classes present in fewer than
    `min_patients` patients keep weight 1.0: they are still learned, still
    predicted, and still reported -- just not amplified.
    """
    counts = np.array([(labels == i).sum() for i in range(cfg.N_CLASSES)], dtype=np.float64)
    counts = np.maximum(counts, 1.0)
    ref = float(counts.max())
    w = np.clip((ref / counts) ** alpha, 1.0, cap)
    for c in range(cfg.N_CLASSES):
        if effective_patients(labels, records, c) < min_patients:
            w[c] = 1.0
    return torch.tensor(w, dtype=torch.float32)


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
        f1 = res[SELECTION_METRIC]
        line = (f"[{tag}] epoch {ep+1:3d}/{epochs}  loss {total/max(nb,1):.4f}  "
                f"val acc {res['accuracy']*100:5.2f}%  macroF1(NSV) {f1*100:5.2f}%  "
                f"S-sens {res['per_class']['S']['sensitivity']*100:5.1f}%  "
                f"S-ppv {res['per_class']['S']['ppv']*100:5.1f}%  "
                f"V-sens {res['per_class']['V']['sensitivity']*100:5.1f}%  "
                f"V-ppv {res['per_class']['V']['ppv']*100:5.1f}%")
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
    weights = class_weights(train_bs.labels, train_bs.records, cfgt.class_weight_alpha)
    print("class weights:", {c: round(float(w), 3)
                             for c, w in zip(("N", "S", "V", "F", "Q"), weights)})

    # --- stage 1: float ----------------------------------------------------
    net = M.CardiaNet()
    f1_float = _run_epochs(net, loader, val_loader, cfgt.float_epochs, cfgt.lr,
                           cfgt.weight_decay, weights, cfgt, "float", log)

    # --- stage 2: fold BatchNorm ------------------------------------------
    folded = M.CardiaNetFolded.from_folded(M.fold_bn(net))
    res_folded = evaluate(folded, val_loader)
    print(f"[fold] val macroF1(NSV) {res_folded[SELECTION_METRIC]*100:.2f}% "
          f"(was {f1_float*100:.2f}%) -- folding is exact, any change here is a bug")
    # QAT trains `folded`'s parameters in place, so snapshot them now. Saving
    # `folded.state_dict()` after QAT would silently store the QAT weights
    # under the "folded" key and make the two stages impossible to compare.
    folded_state = {k: v.detach().clone() for k, v in folded.state_dict().items()}

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
                "folded": folded_state,
                "qat": qat.state_dict()}, out_dir / "cardia.pt")

    summary = {
        "float_val_macro_f1_nsv": f1_float,
        "folded_val_macro_f1_nsv": res_folded[SELECTION_METRIC],
        "qat_val_macro_f1_nsv": f1_qat,
        "qat_val_report": evaluate(qat, val_loader),
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
