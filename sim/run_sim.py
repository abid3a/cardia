#!/usr/bin/env python3
"""Drive the C simulator over MIT-BIH records and check three things.

1. **QRS detection quality.** Score the C detector's R peaks against the
   reference cardiologist annotations with the EC57 150 ms matching window,
   reporting sensitivity and positive predictivity per record and overall.

2. **Host/target numerical parity.** Feed the beat windows and RR features the
   C pipeline produced into the independent numpy integer reference, and
   require the int32 logits to match **exactly**. Not within a tolerance --
   exactly. Two integer implementations written from one specification either
   agree bit for bit or one of them is wrong, and "close enough" would hide the
   rounding-rule bugs this check exists to catch.

3. **End-to-end classification.** Match each C-detected beat to the nearest
   reference annotation and score the AAMI classes, so the reported accuracy
   includes detection errors rather than assuming perfect segmentation.
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ml"))

from cardia import aami, config as cfg, dataset, int_reference, metrics, splits  # noqa: E402

SIM_BIN = ROOT / "sim" / "build" / "cardia_sim"

# uint32 r_index, uint32 class, 4 float rr, 5 int32 logits, 256 float beat
BEAT_FMT = "<II4f5i256f"
BEAT_SIZE = struct.calcsize(BEAT_FMT)


def run_record(record: int, workdir: Path) -> dict:
    rec = dataset.load_record(record)

    samples = workdir / f"{record}.f32"
    samples.write_bytes(rec.raw.astype("<f4").tobytes())
    beats_bin = workdir / f"{record}.beats"
    peaks_txt = workdir / f"{record}.peaks"

    proc = subprocess.run(
        [str(SIM_BIN), str(samples), "--beats", str(beats_bin),
         "--peaks", str(peaks_txt)],
        capture_output=True, text=True, check=True,
    )
    summary = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            summary[parts[0]] = int(parts[1])

    detected = np.array(
        [int(v) for v in peaks_txt.read_text().split()], dtype=np.int64
    ) if peaks_txt.stat().st_size else np.array([], dtype=np.int64)

    # --- 1. QRS detection -------------------------------------------------
    # The detector spends its first two seconds learning thresholds, so beats
    # in that window are excluded from BOTH sides of the comparison rather than
    # scored as misses. Counting them as failures would be measuring the warm-up
    # rather than the algorithm; silently dropping only the detections would be
    # flattering it.
    warmup = 2 * cfg.FS_HZ
    ref = rec.r_indices[rec.r_indices > warmup]
    det = detected[detected > warmup]
    qrs = metrics.score_detections(ref, det, cfg.QRS_MATCH_TOLERANCE_SAMPLES)

    # --- 2. parity: C logits vs numpy integer reference -------------------
    raw = beats_bin.read_bytes()
    n_beats = len(raw) // BEAT_SIZE
    c_idx, c_cls, c_rr, c_log, c_beat = [], [], [], [], []
    for i in range(n_beats):
        f = struct.unpack_from(BEAT_FMT, raw, i * BEAT_SIZE)
        c_idx.append(f[0])
        c_cls.append(f[1])
        c_rr.append(f[2:6])
        c_log.append(f[6:11])
        c_beat.append(f[11:])
    c_idx = np.array(c_idx, dtype=np.int64)
    c_cls = np.array(c_cls, dtype=np.int64)
    c_rr = np.array(c_rr, dtype=np.float32)
    c_log = np.array(c_log, dtype=np.int64)
    c_beat = np.array(c_beat, dtype=np.float32)

    return {
        "record": record,
        "summary": summary,
        "qrs": qrs,
        "n_beats": n_beats,
        "c_idx": c_idx, "c_cls": c_cls, "c_rr": c_rr,
        "c_log": c_log, "c_beat": c_beat,
        "ref_idx": rec.r_indices, "ref_lab": rec.labels,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", default="ds2",
                    help="'ds1', 'ds2', 'all', or a comma-separated list")
    ap.add_argument("--limit", type=int, default=0, help="only the first N records")
    ap.add_argument("--out", default=str(ROOT / "ml" / "artifacts" / "sim_results.json"))
    a = ap.parse_args()

    if not SIM_BIN.exists():
        print(f"error: {SIM_BIN} not built. Run `make -C sim`.", file=sys.stderr)
        return 1

    if a.records == "ds1":
        records = list(splits.DS1)
    elif a.records == "ds2":
        records = list(splits.DS2)
    elif a.records == "all":
        records = list(splits.ALL_RECORDS)
    else:
        records = [int(v) for v in a.records.split(",")]
    if a.limit:
        records = records[: a.limit]

    intnet = int_reference.IntCardiaNet(_load_quant_model())

    tot = dict(tp=0, fp=0, fn=0, reference=0, detected=0)
    parity_total = parity_match = 0
    logit_mismatch = 0
    all_true, all_pred = [], []
    per_record = []
    offsets = []

    with tempfile.TemporaryDirectory() as td:
        workdir = Path(td)
        for r in records:
            res = run_record(r, workdir)
            q = res["qrs"]
            for k in ("tp", "fp", "fn", "reference", "detected"):
                tot[k] += q[k]
            if q["mean_offset_samples"] == q["mean_offset_samples"]:
                offsets.append(q["mean_offset_samples"])

            # --- parity ---------------------------------------------------
            if res["n_beats"]:
                py_log = np.stack([
                    intnet.forward_logits(b, rr)
                    for b, rr in zip(res["c_beat"], res["c_rr"])
                ]).astype(np.int64)
                same = (py_log == res["c_log"]).all(axis=1)
                parity_total += len(same)
                parity_match += int(same.sum())
                logit_mismatch += int((~same).sum())

            # --- end-to-end classification --------------------------------
            t, p = _match_labels(res)
            all_true.append(t)
            all_pred.append(p)

            per_record.append({
                "record": r,
                "qrs_sensitivity": q["sensitivity"],
                "qrs_ppv": q["positive_predictivity"],
                "tp": q["tp"], "fp": q["fp"], "fn": q["fn"],
                "beats_classified": res["n_beats"],
                "mean_offset_samples": q["mean_offset_samples"],
            })
            print(f"  {r}: QRS Se {q['sensitivity']*100:6.2f}%  +P "
                  f"{q['positive_predictivity']*100:6.2f}%  "
                  f"(TP {q['tp']}, FP {q['fp']}, FN {q['fn']})  "
                  f"beats {res['n_beats']}", flush=True)

    se = tot["tp"] / (tot["tp"] + tot["fn"]) if (tot["tp"] + tot["fn"]) else float("nan")
    pp = tot["tp"] / (tot["tp"] + tot["fp"]) if (tot["tp"] + tot["fp"]) else float("nan")

    y_true = np.concatenate(all_true) if all_true else np.array([], dtype=np.int64)
    y_pred = np.concatenate(all_pred) if all_pred else np.array([], dtype=np.int64)
    cls = metrics.summarize(y_true, y_pred) if len(y_true) else {}

    print()
    print(f"QRS detection over {len(records)} records:")
    print(f"  reference beats {tot['reference']}, detections {tot['detected']}")
    print(f"  sensitivity {se*100:.3f}%   positive predictivity {pp*100:.3f}%")
    print(f"  FP {tot['fp']}  FN {tot['fn']}  "
          f"DER {(tot['fp']+tot['fn'])/max(tot['reference'],1)*100:.3f}%")
    print(f"  mean R-peak offset {np.mean(offsets):+.2f} samples "
          f"({np.mean(offsets)/cfg.FS_HZ*1000:+.1f} ms)")
    print()
    print(f"C vs numpy integer reference: {parity_match}/{parity_total} beats "
          f"bit-identical ({parity_match/max(parity_total,1)*100:.4f}%), "
          f"{logit_mismatch} logit mismatches")
    print()
    if cls:
        print(metrics.format_report(cls, "End-to-end (detected beats, not annotated ones)"))

    out = {
        "records": records,
        "qrs_overall": {
            "sensitivity": se, "positive_predictivity": pp,
            **{k: int(v) for k, v in tot.items()},
            "mean_offset_samples": float(np.mean(offsets)) if offsets else None,
        },
        "parity": {
            "beats": parity_total, "bit_identical": parity_match,
            "mismatches": logit_mismatch,
        },
        "end_to_end": cls,
        "per_record": per_record,
    }
    Path(a.out).write_text(json.dumps(out, indent=2, default=float))
    print(f"\nwrote {a.out}")
    return 0 if logit_mismatch == 0 else 1


def _match_labels(res: dict):
    """Pair each C-classified beat with the reference annotation nearest to it."""
    ref_idx, ref_lab = res["ref_idx"], res["ref_lab"]
    tol = cfg.QRS_MATCH_TOLERANCE_SAMPLES
    true, pred = [], []
    for i, cls in zip(res["c_idx"], res["c_cls"]):
        j = int(np.searchsorted(ref_idx, i))
        best, best_d = -1, tol + 1
        for k in (j - 1, j, j + 1):
            if 0 <= k < len(ref_idx):
                d = abs(int(ref_idx[k]) - int(i))
                if d < best_d:
                    best, best_d = k, d
        if best >= 0:
            true.append(int(ref_lab[best]))
            pred.append(int(cls))
    return (np.array(true, dtype=np.int64), np.array(pred, dtype=np.int64))


def _load_quant_model():
    import torch
    from cardia import model as M, quantize as Q
    ck = torch.load(ROOT / "ml" / "artifacts" / "cardia.pt", weights_only=True)
    folded = M.CardiaNetFolded()
    folded.load_state_dict(ck["folded"])
    qat = Q.CardiaNetQAT(folded)
    qat.load_state_dict(ck["qat"])
    qat.eval()
    return Q.export(qat)


if __name__ == "__main__":
    raise SystemExit(main())
