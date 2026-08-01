/* cardia_config.h -- constants shared by the Python reference and the firmware.
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
#define CARDIA_FS_HZ                  360
#define CARDIA_TIM_CLOCK_HZ           90000000u
#define CARDIA_TIM_PSC                49u
#define CARDIA_TIM_ARR                4999u

/* --- Conditioning bandpass (feeds the classifier) --- */
#define CARDIA_BP_LOW_HZ              0.5f
#define CARDIA_BP_HIGH_HZ             40.0f
#define CARDIA_BP_ORDER               4
#define CARDIA_BP_SECTIONS            2

/* --- Detection bandpass (feeds Pan-Tompkins) --- */
#define CARDIA_QRS_BP_LOW_HZ          5.0f
#define CARDIA_QRS_BP_HIGH_HZ         15.0f
#define CARDIA_QRS_BP_ORDER           4
#define CARDIA_QRS_BP_SECTIONS        2

/* --- Beat window --- */
#define CARDIA_BEAT_PRE               100
#define CARDIA_BEAT_POST              156
#define CARDIA_BEAT_LEN               256

/* --- RR features --- */
#define CARDIA_RR_LOCAL_WINDOW        8
#define CARDIA_N_RR_FEATURES          4
#define CARDIA_RR_RATIO_CLIP          3.0f

/* --- Model --- */
#define CARDIA_N_CLASSES              5

/* --- Pan-Tompkins --- */
#define CARDIA_PT_REFRACTORY_SAMPLES  72
#define CARDIA_PT_TWAVE_SAMPLES       130
#define CARDIA_PT_INTEGRATION_SAMPLES 54
#define CARDIA_PT_SEARCHBACK_FACTOR   1.66f

/* --- Evaluation --- */
#define CARDIA_QRS_MATCH_TOL_SAMPLES  54

#endif /* CARDIA_CONFIG_H */
