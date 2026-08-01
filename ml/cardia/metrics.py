"""Evaluation metrics, in the form AAMI EC57 actually asks for.

Accuracy alone is close to useless on this dataset. MIT-BIH is ~90% class N,
so a classifier that outputs "normal" for every beat and never once detects an
arrhythmia scores ~90% accuracy. Any paper or project quoting a single accuracy
figure for MIT-BIH is, deliberately or not, hiding behind that.

What matters clinically is per-class **sensitivity** (of the ectopic beats that
occurred, how many did we catch -- a missed ventricular beat is the dangerous
error) and per-class **positive predictive value** (of the beats we flagged,
how many were real -- poor PPV means alarm fatigue, which in practice means the
alarm gets ignored or switched off).
"""

from __future__ import annotations

import numpy as np

from .aami import AAMI_CLASSES


def confusion_matrix(y_true: np.ndarray, y_pred: np.ndarray, n: int = 5) -> np.ndarray:
    """Rows = true class, columns = predicted class."""
    cm = np.zeros((n, n), dtype=np.int64)
    np.add.at(cm, (y_true.astype(int), y_pred.astype(int)), 1)
    return cm


def per_class(cm: np.ndarray) -> dict[str, dict[str, float]]:
    out: dict[str, dict[str, float]] = {}
    total = cm.sum()
    for i, name in enumerate(AAMI_CLASSES):
        tp = float(cm[i, i])
        fn = float(cm[i, :].sum() - tp)
        fp = float(cm[:, i].sum() - tp)
        tn = float(total - tp - fn - fp)
        sens = tp / (tp + fn) if (tp + fn) > 0 else float("nan")
        # PPV is genuinely undefined when the class was never predicted, and is
        # displayed as such -- but F1 must NOT inherit that undefinedness.
        ppv = tp / (tp + fp) if (tp + fp) > 0 else float("nan")
        spec = tn / (tn + fp) if (tn + fp) > 0 else float("nan")
        # Algebraic form of F1, 2TP / (2TP + FP + FN). Equivalent to the
        # harmonic mean of sensitivity and PPV whenever both are defined, but
        # correctly yields 0 -- not NaN -- when the model never predicts the
        # class at all. Computing F1 from sens/ppv instead lets a NaN escape,
        # and a NaN dropped from a macro average silently REWARDS ignoring a
        # class: a model that never once predicts S scores as if S did not
        # exist. That is exactly the failure this project is about avoiding.
        denom = 2 * tp + fp + fn
        f1 = (2 * tp / denom) if denom > 0 else float("nan")
        out[name] = {
            "support": int(tp + fn),
            "tp": int(tp), "fp": int(fp), "fn": int(fn),
            "sensitivity": sens, "ppv": ppv, "specificity": spec, "f1": f1,
        }
    return out


def summarize(y_true: np.ndarray, y_pred: np.ndarray) -> dict:
    cm = confusion_matrix(y_true, y_pred)
    pc = per_class(cm)
    acc = float((y_true == y_pred).mean())

    # Macro averages over N/S/V/F only. Q is excluded because after removing
    # the paced records it has ~7 beats in the whole test set: including it
    # would let a couple of coin flips swing the headline number by points.
    def macro(metric: str, keys: list[str]) -> float:
        # Every class that OCCURS is included. A class with support that the
        # model never predicts contributes its (zero) F1, it does not vanish.
        vals = []
        for k in keys:
            if pc[k]["support"] == 0:
                continue
            v = pc[k][metric]
            vals.append(0.0 if np.isnan(v) else v)
        return float(np.mean(vals)) if vals else float("nan")

    nsvf = ["N", "S", "V", "F"]
    return {
        "accuracy": acc,
        "n_beats": int(len(y_true)),
        "confusion_matrix": cm.tolist(),
        "per_class": pc,
        "macro_sensitivity": macro("sensitivity", nsvf),
        "macro_ppv": macro("ppv", nsvf),
        "macro_f1": macro("f1", nsvf),
        # Selection metric. F is excluded here and only here: it is 0.8% of
        # MIT-BIH and 90% of DS1's fusion beats live in a single record, so any
        # patient-disjoint validation split has either almost no F beats or
        # almost no F training data. Including a 17-beat class in the epoch
        # selection criterion adds noise, not signal. F is still reported in
        # full for the final DS2 evaluation.
        "macro_f1_nsv": macro("f1", ["N", "S", "V"]),
    }


def format_report(res: dict, title: str = "") -> str:
    lines = []
    if title:
        lines.append(title)
        lines.append("=" * len(title))
    lines.append(f"beats: {res['n_beats']}   overall accuracy: {res['accuracy']*100:.2f}%")
    lines.append("")
    lines.append(f"{'class':<6}{'support':>9}{'sens':>9}{'PPV':>9}{'F1':>9}")
    for name in AAMI_CLASSES:
        m = res["per_class"][name]
        def pct(v):
            return "  --  " if (v != v) else f"{v*100:7.2f}%"
        lines.append(f"{name:<6}{m['support']:>9}{pct(m['sensitivity'])}"
                     f"{pct(m['ppv'])}{pct(m['f1'])}")
    lines.append("")
    lines.append(f"macro (N/S/V/F)  sens {res['macro_sensitivity']*100:.2f}%  "
                 f"PPV {res['macro_ppv']*100:.2f}%  F1 {res['macro_f1']*100:.2f}%")
    lines.append("")
    lines.append("confusion matrix (rows = truth, cols = predicted)")
    header = "      " + "".join(f"{c:>8}" for c in AAMI_CLASSES)
    lines.append(header)
    for i, c in enumerate(AAMI_CLASSES):
        row = "".join(f"{v:>8}" for v in res["confusion_matrix"][i])
        lines.append(f"{c:<6}{row}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# QRS detection scoring (EC57 style)
# ---------------------------------------------------------------------------
def score_detections(reference: np.ndarray, detected: np.ndarray,
                     tolerance: int) -> dict:
    """Match detected R peaks to reference annotations, greedily and in order.

    A detection counts as a true positive if it lands within `tolerance`
    samples (150 ms per EC57) of a reference beat that has not already been
    matched. The one-to-one constraint matters: without it a detector that
    fires five times per QRS would score five true positives instead of one
    true positive and four false ones.
    """
    reference = np.sort(np.asarray(reference, dtype=np.int64))
    detected = np.sort(np.asarray(detected, dtype=np.int64))

    used = np.zeros(len(reference), dtype=bool)
    tp = 0
    errors = []
    j = 0
    for d in detected:
        while j < len(reference) and reference[j] < d - tolerance:
            j += 1
        best, best_dist = -1, tolerance + 1
        k = j
        while k < len(reference) and reference[k] <= d + tolerance:
            if not used[k]:
                dist = abs(int(reference[k]) - int(d))
                if dist < best_dist:
                    best, best_dist = k, dist
            k += 1
        if best >= 0:
            used[best] = True
            tp += 1
            errors.append(int(reference[best]) - int(d))

    fp = len(detected) - tp
    fn = len(reference) - tp
    se = tp / (tp + fn) if (tp + fn) else float("nan")
    pp = tp / (tp + fp) if (tp + fp) else float("nan")
    return {
        "reference": int(len(reference)),
        "detected": int(len(detected)),
        "tp": int(tp), "fp": int(fp), "fn": int(fn),
        "sensitivity": se,
        "positive_predictivity": pp,
        "der": (fp + fn) / len(reference) if len(reference) else float("nan"),
        "mean_offset_samples": float(np.mean(errors)) if errors else float("nan"),
        "std_offset_samples": float(np.std(errors)) if errors else float("nan"),
    }
