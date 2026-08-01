"""Single source of truth for every constant that must agree between the Python
reference implementation and the C firmware.

Anything in here is mirrored into `firmware/src/dsp/cardia_config.h` by
`ml/scripts/gen_config_header.py`. If you change a value here, regenerate the
header -- the host/target parity test in `sim/` will fail loudly otherwise,
which is the point.
"""

from __future__ import annotations

# --- Sampling ---------------------------------------------------------------
# MIT-BIH is recorded at 360 Hz. The firmware samples at exactly the same rate
# rather than resampling to a rounder number: 180 MHz / 360 Hz = 500000 divides
# exactly on TIM2 (PSC=99, ARR=4999), and keeping one rate everywhere removes a
# whole class of Python-vs-C mismatch from the parity test.
FS_HZ: int = 360

# --- Bandpass filter --------------------------------------------------------
# 0.5 Hz high-pass removes baseline wander (respiration ~0.2-0.5 Hz, electrode
# drift, motion). 40 Hz low-pass removes EMG noise and mains hum (50/60 Hz)
# while retaining essentially all diagnostic QRS energy, which lives below
# ~40 Hz. This is the standard AHA monitoring-mode band.
BP_LOW_HZ: float = 0.5
BP_HIGH_HZ: float = 40.0
BP_ORDER: int = 4  # 4th order overall == 2 cascaded biquads

# --- Beat window ------------------------------------------------------------
# 256 samples at 360 Hz = 711 ms, centred asymmetrically on the R peak:
# 100 samples (278 ms) before covers the PR interval and the P wave;
# 156 samples (433 ms) after covers the full ST segment and T wave.
# 256 is a power of two, which keeps the MCU-side ring-buffer maths trivial.
BEAT_PRE: int = 100
BEAT_POST: int = 156
BEAT_LEN: int = BEAT_PRE + BEAT_POST  # 256

# --- RR-interval features ---------------------------------------------------
# Morphology alone cannot separate S beats from N beats: a supraventricular
# ectopic beat is conducted through the normal His-Purkinje system, so its QRS
# looks normal. What makes it ectopic is that it arrives *early*. Timing is
# therefore not an optional extra feature, it is the defining signal for class S.
RR_LOCAL_WINDOW: int = 8  # beats averaged for the causal local-RR baseline
N_RR_FEATURES: int = 4

# Fixed normalisation constants (not dataset statistics) so the MCU can compute
# the identical values without shipping a calibration table.
RR_NORM_SEC: float = 1.0  # divides raw intervals expressed in seconds
RR_RATIO_CLIP: float = 3.0  # ratios are clipped to +/- this before scaling

# --- Model ------------------------------------------------------------------
N_CLASSES: int = 5

# --- Pan-Tompkins -----------------------------------------------------------
# 200 ms physiological refractory period: the ventricles cannot repolarise and
# depolarise again faster than this, so any "detection" sooner is an artefact
# (usually a tall T wave).
PT_REFRACTORY_MS: int = 200
# 360 ms T-wave discrimination window: between 200 and 360 ms after a QRS, a
# candidate peak is checked for slope. A real QRS is steep; a T wave is not.
PT_TWAVE_MS: int = 360
# Moving-window integrator width. ~150 ms is the accepted value: wide enough to
# merge the QRS complex into one hump, narrow enough not to merge QRS with T.
PT_INTEGRATION_MS: int = 150
# Search-back multiplier: if no QRS is found within 1.66 * the running mean RR,
# Pan-Tompkins re-scans the interval with halved thresholds.
PT_SEARCHBACK_FACTOR: float = 1.66

# --- Derived ----------------------------------------------------------------
PT_REFRACTORY_SAMPLES: int = round(PT_REFRACTORY_MS * FS_HZ / 1000)      # 72
PT_TWAVE_SAMPLES: int = round(PT_TWAVE_MS * FS_HZ / 1000)                # 130
PT_INTEGRATION_SAMPLES: int = round(PT_INTEGRATION_MS * FS_HZ / 1000)    # 54

# --- Evaluation -------------------------------------------------------------
# EC57 scores a detection as correct if it falls within 150 ms of the reference
# annotation. Annotators mark the R peak by eye; this tolerance absorbs that.
QRS_MATCH_TOLERANCE_MS: int = 150
QRS_MATCH_TOLERANCE_SAMPLES: int = round(QRS_MATCH_TOLERANCE_MS * FS_HZ / 1000)  # 54
