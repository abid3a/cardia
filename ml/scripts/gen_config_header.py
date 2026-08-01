#!/usr/bin/env python3
"""Mirror ml/cardia/config.py into firmware/src/dsp/cardia_config.h.

Two implementations of one algorithm will drift. The only reliable defence is
to give them one source of truth and generate the other side. CI runs this
script and fails if the working tree changes, so a constant edited in Python
but not regenerated into C cannot reach main.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cardia import config as cfg  # noqa: E402

OUT = Path(__file__).resolve().parents[2] / "firmware" / "src" / "dsp" / "cardia_config.h"

TEMPLATE = """/* cardia_config.h -- constants shared by the Python reference and the firmware.
 *
 * GENERATED FILE. Do not edit by hand.
 * Source of truth: ml/cardia/config.py
 * Regenerate:      python ml/scripts/gen_config_header.py
 *
 * The host/target parity test in sim/ compares C output against the Python
 * reference sample-for-sample, so any drift between these two files shows up
 * as a test failure rather than as a mystery on the bench.
 */

#ifndef CARDIA_CONFIG_H
#define CARDIA_CONFIG_H

/* --- Sampling ---
 * 360 Hz matches MIT-BIH exactly, so no resampling stage exists anywhere in
 * the project.
 *
 * TIM2 lives on APB1. At SYSCLK = 180 MHz the APB1 prescaler is /4, so
 * PCLK1 = 45 MHz -- but because that prescaler is not 1, the timer clock is
 * doubled back to 90 MHz. Missing that doubling is a classic factor-of-two
 * sample-rate bug. 90 MHz / 360 Hz = 250000 = 50 * 5000. */
#define CARDIA_FS_HZ                  {FS_HZ}
#define CARDIA_TIM_CLOCK_HZ           {TIM_CLOCK_HZ}u
#define CARDIA_TIM_PSC                {TIM_PSC}u
#define CARDIA_TIM_ARR                {TIM_ARR}u

/* --- Conditioning bandpass (feeds the classifier) --- */
#define CARDIA_BP_LOW_HZ              {BP_LOW_HZ}f
#define CARDIA_BP_HIGH_HZ             {BP_HIGH_HZ}f
#define CARDIA_BP_ORDER               {BP_ORDER}
#define CARDIA_BP_SECTIONS            {BP_SECTIONS}

/* --- Detection bandpass (feeds Pan-Tompkins) --- */
#define CARDIA_QRS_BP_LOW_HZ          {QRS_BP_LOW_HZ}f
#define CARDIA_QRS_BP_HIGH_HZ         {QRS_BP_HIGH_HZ}f
#define CARDIA_QRS_BP_ORDER           {QRS_BP_ORDER}
#define CARDIA_QRS_BP_SECTIONS        {QRS_BP_SECTIONS}

/* --- Beat window --- */
#define CARDIA_BEAT_PRE               {BEAT_PRE}
#define CARDIA_BEAT_POST              {BEAT_POST}
#define CARDIA_BEAT_LEN               {BEAT_LEN}

/* --- RR features --- */
#define CARDIA_RR_LOCAL_WINDOW        {RR_LOCAL_WINDOW}
#define CARDIA_N_RR_FEATURES          {N_RR_FEATURES}
#define CARDIA_RR_RATIO_CLIP          {RR_RATIO_CLIP}f

/* --- Model --- */
#define CARDIA_N_CLASSES              {N_CLASSES}

/* --- Pan-Tompkins --- */
#define CARDIA_PT_REFRACTORY_SAMPLES  {PT_REFRACTORY_SAMPLES}
#define CARDIA_PT_TWAVE_SAMPLES       {PT_TWAVE_SAMPLES}
#define CARDIA_PT_INTEGRATION_SAMPLES {PT_INTEGRATION_SAMPLES}
#define CARDIA_PT_SEARCHBACK_FACTOR   {PT_SEARCHBACK_FACTOR}f

/* --- Evaluation --- */
#define CARDIA_QRS_MATCH_TOL_SAMPLES  {QRS_MATCH_TOLERANCE_SAMPLES}

#endif /* CARDIA_CONFIG_H */
"""


def main() -> int:
    values = {
        "FS_HZ": cfg.FS_HZ,
        "TIM_CLOCK_HZ": cfg.TIM_CLOCK_HZ,
        "TIM_PSC": cfg.TIM_PSC,
        "TIM_ARR": cfg.TIM_ARR,
        "BP_LOW_HZ": cfg.BP_LOW_HZ,
        "BP_HIGH_HZ": cfg.BP_HIGH_HZ,
        "BP_ORDER": cfg.BP_ORDER,
        "BP_SECTIONS": cfg.BP_ORDER // 2,
        "QRS_BP_LOW_HZ": cfg.QRS_BP_LOW_HZ,
        "QRS_BP_HIGH_HZ": cfg.QRS_BP_HIGH_HZ,
        "QRS_BP_ORDER": cfg.QRS_BP_ORDER,
        "QRS_BP_SECTIONS": cfg.QRS_BP_ORDER // 2,
        "BEAT_PRE": cfg.BEAT_PRE,
        "BEAT_POST": cfg.BEAT_POST,
        "BEAT_LEN": cfg.BEAT_LEN,
        "RR_LOCAL_WINDOW": cfg.RR_LOCAL_WINDOW,
        "N_RR_FEATURES": cfg.N_RR_FEATURES,
        "RR_RATIO_CLIP": cfg.RR_RATIO_CLIP,
        "N_CLASSES": cfg.N_CLASSES,
        "PT_REFRACTORY_SAMPLES": cfg.PT_REFRACTORY_SAMPLES,
        "PT_TWAVE_SAMPLES": cfg.PT_TWAVE_SAMPLES,
        "PT_INTEGRATION_SAMPLES": cfg.PT_INTEGRATION_SAMPLES,
        "PT_SEARCHBACK_FACTOR": cfg.PT_SEARCHBACK_FACTOR,
        "QRS_MATCH_TOLERANCE_SAMPLES": cfg.QRS_MATCH_TOLERANCE_SAMPLES,
    }
    OUT.write_text(TEMPLATE.format(**values))
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
