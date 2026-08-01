#!/usr/bin/env python3
"""Final evaluation, including the honest-vs-inflated comparison.

Produces three numbers that belong together:

  1. **Inter-patient (DS1 -> DS2).** The real result. Trained on 22 patients,
     tested on 22 different patients. This is what the device would do on
     someone it has never seen.

  2. **Integer path on DS2.** The same evaluation run through the numpy integer
     reference rather than the QAT float model, confirming that the number in
     (1) survives quantisation.

  3. **Intra-patient (random beat split).** The number nearly every student
     project and a depressing number of papers report: pool all beats from all
     patients, shuffle, split 80/20. Trained here deliberately, with the same
     architecture and schedule, purely so the gap can be quantified rather than
     asserted.

The difference between (1) and (3) is the point of the whole project.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import (  # noqa: E402
    dataset, int_reference, metrics, model as M, quantize as Q, splits, train as T,
)

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / "ml" / "artifacts"


def load_qat() -> tuple[M.CardiaNetFolded, Q.CardiaNetQAT]:
    ck = torch.load(ART / "cardia.pt", weights_only=True)
    folded = M.CardiaNetFolded()
    folded.load_state_dict(ck["folded"])
    qat = Q.CardiaNetQAT(folded)
    qat.load_state_dict(ck["qat"])
    qat.eval()
    float_only = M.CardiaNetFolded()
    float_only.load_state_dict(ck["folded"])
    float_only.eval()
    return float_only, qat


def loader_for(bs: dataset.BeatSet, batch: int = 4096) -> DataLoader:
    return DataLoader(
        TensorDataset(torch.from_numpy(bs.beats), torch.from_numpy(bs.rr),
                      torch.from_numpy(bs.labels)),
        batch_size=batch, shuffle=False,
    )


def train_intra_patient(cfgt: T.TrainConfig) -> dict:
    """Train the identical architecture on a random 80/20 split of pooled beats.

    Every methodological safeguard is deliberately removed here: patients appear
    on both sides, and consecutive near-duplicate beats from the same recording
    land in both train and test. This is not a baseline to beat, it is a
    demonstration of what the leak is worth.
    """
    ds1 = dataset.load_cached("ds1")
    ds2 = dataset.load_cached("ds2")
    beats = np.concatenate([ds1.beats, ds2.beats])
    rr = np.concatenate([ds1.rr, ds2.rr])
    labels = np.concatenate([ds1.labels, ds2.labels])
    records = np.concatenate([ds1.records, ds2.records])

    rng = np.random.default_rng(cfgt.seed)
    perm = rng.permutation(len(labels))
    cut = int(0.8 * len(labels))
    tr_i, te_i = perm[:cut], perm[cut:]

    tr = dataset.BeatSet(beats[tr_i], rr[tr_i], labels[tr_i], records[tr_i])
    te = dataset.BeatSet(beats[te_i], rr[te_i], labels[te_i], records[te_i])

    shared = len(set(np.unique(tr.records).tolist()) & set(np.unique(te.records).tolist()))
    print(f"[intra] {len(tr)} train / {len(te)} test beats, "
          f"{shared} patients appear in BOTH -- this is the leak")

    T.set_seed(cfgt.seed)
    net = M.CardiaNet()
    weights = T.class_weights(tr.labels, tr.records, cfgt.class_weight_alpha)
    log: list[str] = []
    T._run_epochs(net, DataLoader(
        TensorDataset(torch.from_numpy(tr.beats), torch.from_numpy(tr.rr),
                      torch.from_numpy(tr.labels)),
        batch_size=cfgt.batch_size, shuffle=True, drop_last=True),
        loader_for(te), cfgt.float_epochs, cfgt.lr, cfgt.weight_decay,
        weights, cfgt, "intra", log)

    res = T.evaluate(net, loader_for(te))
    res["shared_patients"] = shared
    return res


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-intra", action="store_true")
    ap.add_argument("--intra-epochs", type=int, default=25)
    a = ap.parse_args()

    float_net, qat = load_qat()
    ds2 = dataset.load_cached("ds2")
    ds1 = dataset.load_cached("ds1")

    out: dict = {}

    # --- 1. inter-patient, float and QAT ----------------------------------
    print("=" * 72)
    res_float = T.evaluate(float_net, loader_for(ds2))
    print(metrics.format_report(res_float, "INTER-PATIENT DS1->DS2  (float model)"))
    print()
    res_qat = T.evaluate(qat, loader_for(ds2))
    print(metrics.format_report(res_qat, "INTER-PATIENT DS1->DS2  (int8 QAT model)"))
    out["inter_patient_float"] = res_float
    out["inter_patient_qat"] = res_qat

    # --- 2. integer reference on DS2 --------------------------------------
    print()
    qm = Q.export(qat)
    intnet = int_reference.IntCardiaNet(qm)
    n = min(12000, len(ds2))
    rng = np.random.default_rng(0)
    idx = np.sort(rng.choice(len(ds2), size=n, replace=False))
    int_pred = intnet.predict_batch(ds2.beats[idx], ds2.rr[idx])
    res_int = metrics.summarize(ds2.labels[idx], int_pred)
    print(metrics.format_report(
        res_int, f"INTER-PATIENT DS1->DS2  (numpy integer path, {n} beats)"))
    with torch.no_grad():
        qat_sub = qat(torch.from_numpy(ds2.beats[idx]),
                      torch.from_numpy(ds2.rr[idx])).argmax(1).numpy()
    agree = float((int_pred == qat_sub).mean())
    print(f"\ninteger path vs QAT float: {agree*100:.3f}% of predictions identical")
    out["integer_path"] = res_int
    out["int_vs_qat_agreement"] = agree

    # --- 3. training-set (DS1) performance, for the overfitting picture ----
    res_ds1 = T.evaluate(qat, loader_for(ds1))
    out["train_set_ds1"] = res_ds1
    print(f"\nFor reference, the same model on its own training patients (DS1): "
          f"accuracy {res_ds1['accuracy']*100:.2f}%, "
          f"S-sens {res_ds1['per_class']['S']['sensitivity']*100:.1f}%, "
          f"V-sens {res_ds1['per_class']['V']['sensitivity']*100:.1f}%")

    # --- 4. the inflated intra-patient number -----------------------------
    if not a.skip_intra:
        print()
        print("=" * 72)
        cfgt = T.TrainConfig(float_epochs=a.intra_epochs, qat_epochs=0)
        res_intra = train_intra_patient(cfgt)
        print()
        print(metrics.format_report(
            res_intra, "INTRA-PATIENT random 80/20 beat split  (THE INFLATED NUMBER)"))
        out["intra_patient"] = res_intra

        print()
        print("=" * 72)
        print(f"  intra-patient accuracy : {res_intra['accuracy']*100:6.2f}%   "
              f"S-sens {res_intra['per_class']['S']['sensitivity']*100:5.1f}%   "
              f"V-sens {res_intra['per_class']['V']['sensitivity']*100:5.1f}%")
        print(f"  inter-patient accuracy : {res_qat['accuracy']*100:6.2f}%   "
              f"S-sens {res_qat['per_class']['S']['sensitivity']*100:5.1f}%   "
              f"V-sens {res_qat['per_class']['V']['sensitivity']*100:5.1f}%")
        print(f"  gap                    : "
              f"{(res_intra['accuracy']-res_qat['accuracy'])*100:6.2f} points of "
              f"accuracy bought entirely by letting patients cross the split")

    out["macs"] = M.mac_count()
    out["params"] = M.param_count()
    out["ds1_records"] = list(splits.DS1)
    out["ds2_records"] = list(splits.DS2)
    (ART / "eval_results.json").write_text(json.dumps(out, indent=2, default=float))
    print(f"\nwrote {ART / 'eval_results.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
