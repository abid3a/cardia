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
# rather than resampling to a rounder number, which removes a whole class of
# Python-vs-C mismatch from the parity test.
#
# The rate divides exactly on the target. Note the APB1 timer-clock rule, which
# is easy to get wrong by a factor of two: at SYSCLK = 180 MHz the APB1
# prescaler is /4, giving PCLK1 = 45 MHz, but because that prescaler is not 1
# the timer clock is doubled to 90 MHz. So TIM2 sees 90 MHz, and
# 90 MHz / 360 Hz = 250000 = 50 * 5000 -> PSC = 49, ARR = 4999.
FS_HZ: int = 360
TIM_CLOCK_HZ: int = 90_000_000
TIM_PSC: int = 49
TIM_ARR: int = 4999

# --- Bandpass filter --------------------------------------------------------
# 0.5 Hz high-pass removes baseline wander (respiration ~0.2-0.5 Hz, electrode
# drift, motion). 40 Hz low-pass removes EMG noise and mains hum (50/60 Hz)
# while retaining essentially all diagnostic QRS energy, which lives below
# ~40 Hz. This is the standard AHA monitoring-mode band.
BP_LOW_HZ: float = 0.5
BP_HIGH_HZ: float = 40.0
BP_ORDER: int = 4  # 4th order overall == 2 cascaded biquads

# --- QRS-detection bandpass (Pan-Tompkins front end) ------------------------
# A SECOND, narrower filter, running in parallel on the same raw stream.
# This is not redundancy. The two filters have opposite goals:
#   * 0.5-40 Hz feeds the classifier and must PRESERVE morphology -- the P and
#     T waves are the whole point, and they live at 0.5-10 Hz.
#   * 5-15 Hz feeds the detector and must DESTROY morphology -- it deliberately
#     suppresses P and T so that the QRS is the only thing left standing,
#     which is exactly what makes a simple amplitude threshold work.
# Pan & Tompkins (1985) chose this band because QRS spectral energy peaks near
# 10 Hz while P/T energy and baseline wander sit below ~5 Hz and muscle noise
# above ~20 Hz.
QRS_BP_LOW_HZ: float = 5.0
QRS_BP_HIGH_HZ: float = 15.0
QRS_BP_ORDER: int = 4

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
