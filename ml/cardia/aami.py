"""AAMI EC57 beat-class mapping for the MIT-BIH Arrhythmia Database.

ANSI/AAMI EC57 does not grade an algorithm on the 15+ raw MIT-BIH beat labels.
It collapses them into five heartbeat classes, because that is the granularity a
monitoring device is actually expected to act on:

    N  Normal / bundle-branch-block / escape beats originating at or above the AV
       node whose conduction is otherwise normal.
    S  Supraventricular ectopic beat (originates above the ventricles, early).
    V  Ventricular ectopic beat (originates in the ventricles, wide QRS).
    F  Fusion of a ventricular and a normal beat.
    Q  Unclassifiable / paced.

The mapping below is the one used by de Chazal, O'Dwyer & Reilly (2004), which
has become the de-facto reference for inter-patient MIT-BIH evaluation. Getting
this table wrong silently changes the difficulty of the problem, so it is kept
in one place and unit-tested.
"""

from __future__ import annotations

# --- The five AAMI classes, in a fixed order used everywhere (model outputs,
# --- confusion matrices, C code generation). Do not reorder.
AAMI_CLASSES: tuple[str, ...] = ("N", "S", "V", "F", "Q")
CLASS_INDEX: dict[str, int] = {c: i for i, c in enumerate(AAMI_CLASSES)}

AAMI_CLASS_NAMES: dict[str, str] = {
    "N": "Normal",
    "S": "Supraventricular ectopic",
    "V": "Ventricular ectopic",
    "F": "Fusion",
    "Q": "Unclassifiable / paced",
}

# --- MIT-BIH annotation symbol -> AAMI class -------------------------------
SYMBOL_TO_AAMI: dict[str, str] = {
    # N: normal and conduction-defect beats
    "N": "N",  # Normal beat
    "L": "N",  # Left bundle branch block beat
    "R": "N",  # Right bundle branch block beat
    "e": "N",  # Atrial escape beat
    "j": "N",  # Nodal (junctional) escape beat
    # S: supraventricular ectopic beats
    "A": "S",  # Atrial premature beat
    "a": "S",  # Aberrated atrial premature beat
    "J": "S",  # Nodal (junctional) premature beat
    "S": "S",  # Supraventricular premature beat
    # V: ventricular ectopic beats
    "V": "V",  # Premature ventricular contraction
    "E": "V",  # Ventricular escape beat
    # F: fusion beats
    "F": "F",  # Fusion of ventricular and normal beat
    # Q: unclassifiable / paced
    "/": "Q",  # Paced beat
    "f": "Q",  # Fusion of paced and normal beat
    "Q": "Q",  # Unclassifiable beat
}

# Annotation symbols that mark rhythm changes, signal-quality changes or
# artefacts rather than heartbeats. They carry no beat label and must be
# skipped, not mapped to a class.
NON_BEAT_SYMBOLS: frozenset[str] = frozenset(
    "+~|sT*D=\"p^?![]xn( )@"
)


def is_beat_symbol(symbol: str) -> bool:
    """True if `symbol` annotates an actual heartbeat we can label."""
    return symbol in SYMBOL_TO_AAMI


def to_aami(symbol: str) -> str | None:
    """Map a MIT-BIH annotation symbol to its AAMI class, or None if not a beat."""
    return SYMBOL_TO_AAMI.get(symbol)


def to_index(symbol: str) -> int | None:
    """Map a MIT-BIH annotation symbol to an AAMI class index, or None."""
    cls = SYMBOL_TO_AAMI.get(symbol)
    return None if cls is None else CLASS_INDEX[cls]
