"""MIT-BIH Arrhythmia Database access, caching and beat-set construction.

Reference: Moody GB, Mark RG. "The impact of the MIT-BIH Arrhythmia Database."
IEEE Eng in Med and Biol 20(3):45-50 (2001). Distributed by PhysioNet.

The database is 48 half-hour two-lead recordings at 360 Hz with 11-bit
resolution, beat-by-beat annotated by two independent cardiologists. Its value
is the annotations: ~110,000 individually labelled beats, which is what makes
supervised beat classification possible at all.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from . import aami, config as cfg, preprocess, splits

DEFAULT_DATA_DIR = Path(__file__).resolve().parents[2] / "data" / "mitdb"

# Most MIT-BIH records use the modified-limb-lead II (MLII) as channel 0, which
# is the lead a single-channel AD8232 front end with standard limb electrode
# placement produces. A few records order the channels differently, so the lead
# is selected by *name* rather than by index -- selecting channel 0 blindly
# would silently feed a precordial lead into a model trained on MLII.
PREFERRED_LEADS = ("MLII", "II", "ML II")


def download(records=None, data_dir: Path = DEFAULT_DATA_DIR) -> Path:
    """Fetch the required records from PhysioNet into a local cache."""
    import wfdb

    records = list(records or splits.ALL_RECORDS)
    data_dir.mkdir(parents=True, exist_ok=True)

    missing = [r for r in records if not (data_dir / f"{r}.dat").exists()]
    if not missing:
        return data_dir

    wfdb.dl_database(
        "mitdb",
        str(data_dir),
        records=[str(r) for r in missing],
        annotators=["atr"],
        overwrite=False,
    )
    return data_dir


def _pick_channel(sig_names: list[str]) -> int:
    for want in PREFERRED_LEADS:
        for i, name in enumerate(sig_names):
            if name.strip().upper() == want.upper():
                return i
    return 0


@dataclass
class Record:
    """One MIT-BIH record, already reduced to what the pipeline needs."""

    number: int
    fs: int
    lead: str
    raw: np.ndarray            # millivolts, as digitised
    filtered: np.ndarray       # after causal 0.5-40 Hz bandpass
    r_indices: np.ndarray      # reference R-peak sample indices (beats only)
    symbols: list[str]         # raw MIT-BIH symbol per reference beat
    labels: np.ndarray         # AAMI class index per reference beat
    all_ann_indices: np.ndarray = field(default_factory=lambda: np.array([], dtype=np.int64))


def load_record(number: int, data_dir: Path = DEFAULT_DATA_DIR) -> Record:
    """Read one record, keep a single lead, bandpass it, and align annotations."""
    import wfdb

    path = str(data_dir / str(number))
    rec = wfdb.rdrecord(path)
    ann = wfdb.rdann(path, "atr")

    ch = _pick_channel(list(rec.sig_name))
    raw = np.asarray(rec.p_signal[:, ch], dtype=np.float32)

    if int(rec.fs) != cfg.FS_HZ:
        raise ValueError(
            f"record {number} is {rec.fs} Hz; the whole pipeline assumes {cfg.FS_HZ} Hz"
        )

    keep = [
        (int(s), sym)
        for s, sym in zip(ann.sample, ann.symbol)
        if aami.is_beat_symbol(sym)
    ]
    r_indices = np.array([s for s, _ in keep], dtype=np.int64)
    symbols = [sym for _, sym in keep]
    labels = np.array([aami.CLASS_INDEX[aami.SYMBOL_TO_AAMI[s]] for s in symbols], dtype=np.int64)

    return Record(
        number=number,
        fs=int(rec.fs),
        lead=rec.sig_name[ch],
        raw=raw,
        filtered=preprocess.bandpass_filter(raw),
        r_indices=r_indices,
        symbols=symbols,
        labels=labels,
        all_ann_indices=np.asarray(ann.sample, dtype=np.int64),
    )


@dataclass
class BeatSet:
    """Windowed, normalised beats plus their timing features and labels."""

    beats: np.ndarray     # (n, BEAT_LEN) float32, per-beat z-scored
    rr: np.ndarray        # (n, N_RR_FEATURES) float32
    labels: np.ndarray    # (n,) int64, AAMI class index
    records: np.ndarray   # (n,) int64, source record number -- needed to prove
                          # no patient crosses the train/test boundary

    def __len__(self) -> int:
        return len(self.labels)

    def counts(self) -> dict[str, int]:
        return {
            cls: int((self.labels == i).sum())
            for i, cls in enumerate(aami.AAMI_CLASSES)
        }


def beats_from_record(rec: Record) -> BeatSet:
    """Window every annotated beat in a record.

    Beat *segmentation* uses the reference annotation's R index, not a detected
    one. This separates the two problems on purpose: QRS detection is scored on
    its own (sensitivity / positive predictivity, see `sim/`), and the
    classifier is scored on correctly located beats. Mixing them would make a
    classification error indistinguishable from a detection error.
    """
    rr = preprocess.rr_features(rec.r_indices, fs=rec.fs)

    beats, feats, labels = [], [], []
    for i, r in enumerate(rec.r_indices):
        win = preprocess.extract_beat(rec.filtered, int(r))
        if win is None:
            continue
        beats.append(preprocess.normalize_beat(win))
        feats.append(rr[i])
        labels.append(rec.labels[i])

    n = len(beats)
    return BeatSet(
        beats=np.asarray(beats, dtype=np.float32).reshape(n, cfg.BEAT_LEN),
        rr=np.asarray(feats, dtype=np.float32).reshape(n, cfg.N_RR_FEATURES),
        labels=np.asarray(labels, dtype=np.int64),
        records=np.full(n, rec.number, dtype=np.int64),
    )


def concat(sets: list[BeatSet]) -> BeatSet:
    return BeatSet(
        beats=np.concatenate([s.beats for s in sets]),
        rr=np.concatenate([s.rr for s in sets]),
        labels=np.concatenate([s.labels for s in sets]),
        records=np.concatenate([s.records for s in sets]),
    )


def build_split(
    records, data_dir: Path = DEFAULT_DATA_DIR, verbose: bool = True
) -> BeatSet:
    out = []
    for r in records:
        rec = load_record(r, data_dir)
        bs = beats_from_record(rec)
        if verbose:
            print(f"  record {r:3d}  lead {rec.lead:5s}  {len(bs):5d} beats  {bs.counts()}")
        out.append(bs)
    return concat(out)


def cache_path(data_dir: Path, name: str) -> Path:
    return data_dir.parent / "cache" / f"{name}.npz"


def build_and_cache(data_dir: Path = DEFAULT_DATA_DIR, verbose: bool = True) -> dict:
    """Build DS1 and DS2 beat sets and cache them as .npz.

    Also asserts the invariant the whole project rests on: the set of record
    numbers in DS1 and the set in DS2 do not intersect.
    """
    out = {}
    for name, recs in (("ds1", splits.DS1), ("ds2", splits.DS2)):
        if verbose:
            print(f"[{name.upper()}] {len(recs)} records")
        bs = build_split(recs, data_dir, verbose)
        p = cache_path(data_dir, name)
        p.parent.mkdir(parents=True, exist_ok=True)
        np.savez_compressed(
            p, beats=bs.beats, rr=bs.rr, labels=bs.labels, records=bs.records
        )
        out[name] = bs
        if verbose:
            print(f"[{name.upper()}] total {len(bs)} beats {bs.counts()} -> {p.name}")

    train_patients = set(np.unique(out["ds1"].records).tolist())
    test_patients = set(np.unique(out["ds2"].records).tolist())
    leak = train_patients & test_patients
    assert not leak, f"PATIENT LEAKAGE between DS1 and DS2: {sorted(leak)}"

    summary = {
        "ds1_beats": len(out["ds1"]),
        "ds2_beats": len(out["ds2"]),
        "ds1_counts": out["ds1"].counts(),
        "ds2_counts": out["ds2"].counts(),
        "ds1_patients": sorted(train_patients),
        "ds2_patients": sorted(test_patients),
        "patient_overlap": sorted(leak),
    }
    (data_dir.parent / "cache" / "summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def load_cached(name: str, data_dir: Path = DEFAULT_DATA_DIR) -> BeatSet:
    d = np.load(cache_path(data_dir, name))
    return BeatSet(
        beats=d["beats"], rr=d["rr"], labels=d["labels"], records=d["records"]
    )
