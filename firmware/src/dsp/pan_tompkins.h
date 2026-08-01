/* pan_tompkins.h -- streaming QRS detector.
 *
 * Pan J, Tompkins WJ. "A Real-Time QRS Detection Algorithm."
 * IEEE Trans Biomed Eng BME-32(3):230-236, 1985.
 *
 * Why this algorithm and not something newer (wavelet, Hilbert transform, a
 * second neural net):
 *   * O(1) per sample with a fixed, tiny state footprint -- no FFT, no
 *     buffering of a whole beat, no dynamic allocation. That is the shape a
 *     hard-real-time interrupt-driven system needs.
 *   * Its thresholds ADAPT. Electrode contact degrades, the patient moves,
 *     signal amplitude drifts by an order of magnitude across a recording. A
 *     fixed threshold fails within minutes; the running signal/noise peak
 *     estimates here track it.
 *   * It still reports >99% sensitivity on MIT-BIH, which nothing has
 *     meaningfully beaten for this compute budget.
 *
 * Everything here is portable C: the same source runs on the host for the
 * simulator and unit tests, and on the Cortex-M4F. No MCU headers, no
 * allocation, no libm.
 */

#ifndef CARDIA_PAN_TOMPKINS_H
#define CARDIA_PAN_TOMPKINS_H

#include <stdint.h>
#include <stddef.h>

#include "cardia_config.h"
#include "biquad.h"
#include "filters.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Search-back can retroactively emit a missed beat in the same step a normal
 * detection fires, so one call may produce up to two R peaks. */
#define CARDIA_PT_MAX_EMIT 2

/* Candidate peaks that fell below threshold are remembered so search-back has
 * something to re-examine. Sized for a ~3 s worst-case gap at 360 Hz. */
#define CARDIA_PT_MAX_CANDIDATES 24

/* Ring buffers of the two filtered signals. Must exceed the integrator width
 * plus the derivative delay plus margin; a power of two makes the wrap a mask.
 */
#define CARDIA_PT_HIST_LEN 128
#define CARDIA_PT_HIST_MASK (CARDIA_PT_HIST_LEN - 1)

/* Number of RR intervals in each running mean. Eight, per the paper. */
#define CARDIA_PT_RR_WINDOW 8

/* Duration of the initial threshold-estimation phase. */
#define CARDIA_PT_LEARNING_SAMPLES (2 * CARDIA_FS_HZ)

typedef struct {
    uint32_t index;  /* absolute sample number of the refined R peak */
    float    mwi;    /* integrator amplitude at the peak */
    float    bp;     /* detection-band amplitude at the peak */
    float    slope;  /* |max first difference| near the peak */
} cardia_pt_candidate_t;

typedef struct {
    /* --- stage 1: detection bandpass, 5-15 Hz --- */
    cardia_qrs_bandpass_t bp;

    /* --- stage 2: five-point derivative delay line, x[n-1]..x[n-4] --- */
    float deriv_hist[4];

    /* --- stage 4: moving-window integrator --- */
    float  mwi_ring[CARDIA_PT_INTEGRATION_SAMPLES];
    size_t mwi_pos;
    float  mwi_sum;

    /* last three integrator outputs, for local-maximum detection */
    float mwi_prev2, mwi_prev1;

    /* signal histories, for peak refinement and slope measurement */
    float bp_hist[CARDIA_PT_HIST_LEN];    /* 5-15 Hz detection band */
    float cond_hist[CARDIA_PT_HIST_LEN];  /* 0.5-40 Hz conditioning band */

    /* --- adaptive thresholds (paper notation) --- */
    float spki, npki;  /* signal / noise peak estimates, integrator */
    float spkf, npkf;  /* signal / noise peak estimates, filtered signal */

    /* --- rhythm tracking --- */
    uint32_t rr1[CARDIA_PT_RR_WINDOW];  /* last 8 intervals, unconditional */
    uint32_t rr2[CARDIA_PT_RR_WINDOW];  /* last 8 intervals judged "normal" */
    size_t   rr1_count, rr2_count, rr1_pos, rr2_pos;
    float    rr_mean1, rr_mean2;

    uint32_t n;               /* samples consumed so far */
    uint32_t last_qrs;        /* sample index of the most recent accepted R peak */
    int      have_qrs;        /* 0 until the first beat is accepted */
    float    last_qrs_slope;  /* |slope| of the most recent accepted QRS */

    cardia_pt_candidate_t candidates[CARDIA_PT_MAX_CANDIDATES];
    size_t n_candidates;

    /* --- learning phase accumulators --- */
    int   learning;
    float learn_mwi_max, learn_mwi_sum;
    float learn_bp_max, learn_bp_sum;

    uint32_t beats_found;
} cardia_pt_t;

void cardia_pt_init(cardia_pt_t *pt);

/* Feed one sample.
 *
 *   raw   the unfiltered sample (drives the detector's own 5-15 Hz filter)
 *   cond  the same sample after the 0.5-40 Hz conditioning filter
 *
 * `cond` is passed in rather than recomputed because the caller already
 * maintains it for the classifier, and because the reported R index must point
 * at the peak of the *same* signal the beat window is cut from -- otherwise the
 * detector's group delay would offset every beat window by a few samples.
 *
 * Returns the number of R peaks emitted (0..2) and writes their absolute
 * sample indices to `out`. Indices always refer to samples already consumed:
 * the integrator cannot know a QRS happened until it has integrated over it,
 * so detection is inherently retrospective by CARDIA_PT_REPORT_LATENCY.
 */
int cardia_pt_step(cardia_pt_t *pt, float raw, float cond,
                   uint32_t out[CARDIA_PT_MAX_EMIT]);

/* Worst-case delay, in samples, between the true R peak and the step that can
 * report it. Used to size the beat buffer so a full window is still available
 * when the R peak is announced. */
#define CARDIA_PT_REPORT_LATENCY (CARDIA_PT_INTEGRATION_SAMPLES + 8)

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_PAN_TOMPKINS_H */
