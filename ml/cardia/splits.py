"""Inter-patient DS1/DS2 record split (de Chazal et al., 2004).

Why this file exists at all
---------------------------
The default way to evaluate an MIT-BIH classifier -- pool every beat from every
record, shuffle, then take a random 80/20 split -- is wrong, and it is wrong in
a way that inflates accuracy to 98-99%. Consecutive beats from one patient are
near-duplicates: same electrode placement, same body habitus, same conduction
pathology, often the same ectopic focus firing repeatedly. A random split puts
some of a patient's beats in train and the rest in test, so at test time the
model is recognising the *patient*, not the *arrhythmia*. That is textbook data
leakage, and the resulting number tells you nothing about how the device would
behave on the next patient who walks in -- which is the only thing that matters.

The inter-patient protocol fixes this by splitting on *records* (patients), not
beats. DS1 and DS2 below are the standard, published, disjoint record lists.
Every beat from a DS1 patient is available for training; every beat from a DS2
patient is held out for testing; no patient appears in both.

Paced records (102, 104, 107, 217) are excluded per AAMI EC57, which states that
devices are not required to classify beats from paced patients: the pacing
spike dominates the morphology and tells you about the pacemaker rather than
the heart.
"""

from __future__ import annotations

# Training set -- 22 records (de Chazal et al. 2004, Table II).
DS1: tuple[int, ...] = (
    101, 106, 108, 109, 112, 114, 115, 116, 118, 119, 122,
    124, 201, 203, 205, 207, 208, 209, 215, 220, 223, 230,
)

# Test set -- 22 records, disjoint from DS1.
DS2: tuple[int, ...] = (
    100, 103, 105, 111, 113, 117, 121, 123, 200, 202, 210,
    212, 213, 214, 219, 221, 222, 228, 231, 232, 233, 234,
)

# Excluded from both sets per AAMI EC57 (paced rhythms).
PACED_RECORDS: tuple[int, ...] = (102, 104, 107, 217)

ALL_RECORDS: tuple[int, ...] = tuple(sorted(DS1 + DS2))


def _validate() -> None:
    """Fail loudly at import time if the split is ever edited into an invalid state."""
    assert len(DS1) == 22, f"DS1 must hold 22 records, got {len(DS1)}"
    assert len(DS2) == 22, f"DS2 must hold 22 records, got {len(DS2)}"
    overlap = set(DS1) & set(DS2)
    assert not overlap, f"DS1/DS2 must be disjoint, overlap={sorted(overlap)}"
    paced_leak = (set(DS1) | set(DS2)) & set(PACED_RECORDS)
    assert not paced_leak, f"paced records must be excluded, found {sorted(paced_leak)}"


_validate()


def split_for(record: int) -> str:
    """Return 'DS1', 'DS2' or 'excluded' for a record number."""
    if record in DS1:
        return "DS1"
    if record in DS2:
        return "DS2"
    return "excluded"
