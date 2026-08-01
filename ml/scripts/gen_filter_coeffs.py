#!/usr/bin/env python3
"""Emit the fixed filter coefficients as a C source file.

The filters are *designed* in scipy (bilinear transform, Butterworth prototype)
and *executed* in C. Designing on the MCU would mean shipping a pole-placement
routine to compute ten constants that never change -- pointless. Freezing the
coefficients also guarantees the host and the target run numerically identical
filters, which is what makes the parity check in sim/ a real test rather than a
comparison of two slightly different algorithms.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import config as cfg  # noqa: E402
from cardia.preprocess import bandpass_sos, qrs_bandpass_sos  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "firmware" / "src" / "dsp" / "filter_coeffs.c"

PREAMBLE = f"""/* filter_coeffs.c -- fixed IIR coefficients for the Cardia signal chain.
 *
 * GENERATED FILE. Do not edit by hand.
 * Regenerate: python ml/scripts/gen_filter_coeffs.py
 *
 * Both filters are Butterworth, designed with scipy.signal.butter(...,
 * output='sos') at fs = {cfg.FS_HZ} Hz, and stored as scipy `sos` rows with a0
 * divided out:
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 */

#include "filters.h"

"""


def emit(name: str, sos, comment: str) -> str:
    rows = []
    for row in sos:
        b0, b1, b2, a0, a1, a2 = row
        b0, b1, b2, a1, a2 = (v / a0 for v in (b0, b1, b2, a1, a2))
        rows.append(
            "    {{ {:+.9e}f, {:+.9e}f, {:+.9e}f, {:+.9e}f, {:+.9e}f }},".format(
                b0, b1, b2, a1, a2
            )
        )
    body = "\n".join(rows)
    return f"/* {comment} */\nconst cardia_biquad_coeffs_t {name}[] = {{\n{body}\n}};\n\n"


def main() -> int:
    cond = bandpass_sos()
    qrs = qrs_bandpass_sos()

    if cond.shape[0] != cfg.BP_ORDER // 2 or qrs.shape[0] != cfg.QRS_BP_ORDER // 2:
        raise SystemExit("unexpected section count; check config vs header")

    text = PREAMBLE
    text += emit(
        "cardia_bandpass_sos",
        cond,
        f"{cfg.BP_LOW_HZ}-{cfg.BP_HIGH_HZ} Hz conditioning band -- feeds the classifier, "
        "preserves P/QRS/T morphology",
    )
    text += emit(
        "cardia_qrs_bandpass_sos",
        qrs,
        f"{cfg.QRS_BP_LOW_HZ}-{cfg.QRS_BP_HIGH_HZ} Hz detection band -- feeds Pan-Tompkins, "
        "deliberately suppresses P and T",
    )
    OUT.write_text(text)

    print(f"wrote {OUT}")
    for label, sos in (("conditioning 0.5-40", cond), ("detection 5-15", qrs)):
        print(f"  {label}:")
        for i, row in enumerate(sos):
            print(f"    section {i}: b={row[:3]}  a={row[3:]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
