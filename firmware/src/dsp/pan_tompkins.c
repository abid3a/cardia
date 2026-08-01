#include "pan_tompkins.h"

#include <string.h>

/* Build with -DCARDIA_PT_DEBUG to trace every candidate peak and the decision
 * made about it. Never compiled into the firmware; it exists because the only
 * way to tune the T-wave rule is to see the numbers it is comparing. */
#ifdef CARDIA_PT_DEBUG
#include <stdio.h>
#define PT_TRACE(...) fprintf(stderr, __VA_ARGS__)
#else
#define PT_TRACE(...) ((void)0)
#endif

/* ------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* ------------------------------------------------------------------------- */

static inline float f_abs(float v) { return v < 0.0f ? -v : v; }

static inline float hist_at(const float *hist, uint32_t index)
{
    return hist[index & CARDIA_PT_HIST_MASK];
}

/* ------------------------------------------------------------------------- */
/* Init                                                                       */
/* ------------------------------------------------------------------------- */

void cardia_pt_init(cardia_pt_t *pt)
{
    memset(pt, 0, sizeof(*pt));
    cardia_qrs_bandpass_init(&pt->bp);
    pt->learning = 1;
    /* A sane default so the very first RR-based decisions are not divisions by
     * zero: 60 bpm. Both means are replaced as soon as two beats are seen. */
    pt->rr_mean1 = (float)CARDIA_FS_HZ;
    pt->rr_mean2 = (float)CARDIA_FS_HZ;
}

/* ------------------------------------------------------------------------- */
/* RR bookkeeping                                                             */
/* ------------------------------------------------------------------------- */

static float mean_u32(const uint32_t *v, size_t count)
{
    if (count == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < count; ++i) sum += (float)v[i];
    return sum / (float)count;
}

static void rr_update(cardia_pt_t *pt, uint32_t interval)
{
    /* RR AVERAGE 1: the last eight intervals, no questions asked. Tracks the
     * actual rhythm including ectopy. */
    pt->rr1[pt->rr1_pos] = interval;
    pt->rr1_pos = (pt->rr1_pos + 1) % CARDIA_PT_RR_WINDOW;
    if (pt->rr1_count < CARDIA_PT_RR_WINDOW) pt->rr1_count++;
    pt->rr_mean1 = mean_u32(pt->rr1, pt->rr1_count);

    /* RR AVERAGE 2: only intervals that look like normal sinus rhythm, i.e.
     * within [92%, 116%] of the current normal mean. This second mean is what
     * search-back is measured against. Keeping ectopic intervals out of it
     * matters: a run of bigeminy would otherwise drag the "expected" interval
     * down and stop search-back from ever firing. */
    const float lo = 0.92f * pt->rr_mean2;
    const float hi = 1.16f * pt->rr_mean2;
    if (pt->rr2_count == 0 || ((float)interval >= lo && (float)interval <= hi)) {
        pt->rr2[pt->rr2_pos] = interval;
        pt->rr2_pos = (pt->rr2_pos + 1) % CARDIA_PT_RR_WINDOW;
        if (pt->rr2_count < CARDIA_PT_RR_WINDOW) pt->rr2_count++;
        pt->rr_mean2 = mean_u32(pt->rr2, pt->rr2_count);
    }
}

/* When the rhythm is irregular, the paper halves both thresholds so that
 * detection keeps up with rapidly changing amplitude. */
static int rhythm_irregular(const cardia_pt_t *pt)
{
    if (pt->rr2_count == 0) return 0;
    const float lo = 0.92f * pt->rr_mean2;
    const float hi = 1.16f * pt->rr_mean2;
    return (pt->rr_mean1 < lo || pt->rr_mean1 > hi);
}

/* ------------------------------------------------------------------------- */
/* Peak refinement                                                            */
/* ------------------------------------------------------------------------- */

/* Given an integrator local maximum at `mwi_index`, locate the actual R peak.
 *
 * The integrator output at time n is the mean of the squared derivative over
 * the preceding CARDIA_PT_INTEGRATION_SAMPLES, and the derivative itself is
 * delayed by 2 samples. So the QRS that produced this hump lies inside
 * [mwi_index - N - 2, mwi_index - 2]. Search that window for the largest
 * |amplitude| in the conditioning band -- the band the beat window will
 * actually be cut from. */
/* Half-width of the window used to measure a candidate's slope, in samples.
 * ~28 ms either side of the fiducial point: wide enough to contain the whole
 * up- or down-stroke of even a broad ventricular complex, narrow enough that
 * two candidates 200 ms apart cannot share a single steep edge.
 *
 * That sharing was a real bug, not a hypothetical one. Measuring the slope
 * across the full 56-sample search window meant a wide PVC -- whose squared
 * derivative produces two integrator humps, one for the R upstroke and one for
 * the deep S downstroke, separated by more than the 200 ms refractory -- had
 * both humps report the *same* maximum slope. The T-wave rule compares the
 * candidate's slope against the last accepted QRS's, so the ratio came out at
 * exactly 1.000 and the rule could never fire. Record 119, which is largely
 * ventricular bigeminy, produced 442 false detections from this alone. */
#define PT_SLOPE_HALF_WIDTH 10u

static void refine_peak(const cardia_pt_t *pt, uint32_t mwi_index,
                        cardia_pt_candidate_t *out)
{
    const uint32_t width = CARDIA_PT_INTEGRATION_SAMPLES + 2u;
    const uint32_t stop = (mwi_index >= 2u) ? (mwi_index - 2u) : 0u;
    const uint32_t start = (stop >= width) ? (stop - width) : 0u;

    /* Pass 1: locate the fiducial point -- the largest excursion in the
     * conditioning band, which is the band the beat window is cut from. */
    uint32_t best_i = start;
    float best_abs = -1.0f;
    for (uint32_t i = start; i <= stop; ++i) {
        const float a = f_abs(hist_at(pt->cond_hist, i));
        if (a > best_abs) {
            best_abs = a;
            best_i = i;
        }
    }

    /* Pass 2: characterise the candidate in a tight window around its OWN
     * fiducial point, not across the whole search span. */
    const uint32_t lo = (best_i > start + PT_SLOPE_HALF_WIDTH)
                            ? (best_i - PT_SLOPE_HALF_WIDTH) : start;
    const uint32_t hi = (best_i + PT_SLOPE_HALF_WIDTH < stop)
                            ? (best_i + PT_SLOPE_HALF_WIDTH) : stop;

    float best_bp = 0.0f;
    float max_slope = 0.0f;
    for (uint32_t i = lo; i <= hi; ++i) {
        const float b = f_abs(hist_at(pt->bp_hist, i));
        if (b > best_bp) best_bp = b;
        if (i > lo) {
            const float d = f_abs(hist_at(pt->bp_hist, i) - hist_at(pt->bp_hist, i - 1u));
            if (d > max_slope) max_slope = d;
        }
    }

    out->index = best_i;
    out->mwi = 0.0f; /* filled by the caller */
    out->bp = best_bp;
    out->slope = max_slope;
}

/* ------------------------------------------------------------------------- */
/* Main step                                                                  */
/* ------------------------------------------------------------------------- */

static void accept_qrs(cardia_pt_t *pt, const cardia_pt_candidate_t *c,
                       int from_searchback, uint32_t out[CARDIA_PT_MAX_EMIT],
                       int *n_out)
{
    if (from_searchback) {
        /* A beat recovered by search-back was, by definition, below the normal
         * threshold. Adapt faster (1/4 instead of 1/8) so the estimate catches
         * up to a signal that has clearly shrunk. */
        pt->spki = 0.25f * c->mwi + 0.75f * pt->spki;
        pt->spkf = 0.25f * c->bp + 0.75f * pt->spkf;
    } else {
        pt->spki = 0.125f * c->mwi + 0.875f * pt->spki;
        pt->spkf = 0.125f * c->bp + 0.875f * pt->spkf;
    }

    if (pt->have_qrs && c->index > pt->last_qrs) {
        rr_update(pt, c->index - pt->last_qrs);
    }
    pt->last_qrs = c->index;
    pt->last_qrs_slope = c->slope;
    pt->have_qrs = 1;
    pt->beats_found++;
    pt->n_candidates = 0;

    if (*n_out < CARDIA_PT_MAX_EMIT) {
        out[(*n_out)++] = c->index;
    }
}

int cardia_pt_step(cardia_pt_t *pt, float raw, float cond,
                   uint32_t out[CARDIA_PT_MAX_EMIT])
{
    int n_out = 0;
    const uint32_t n = pt->n;

    /* --- stage 1: 5-15 Hz detection band ---------------------------------- */
    const float bp = cardia_qrs_bandpass_step(&pt->bp, raw);
    pt->bp_hist[n & CARDIA_PT_HIST_MASK] = bp;
    pt->cond_hist[n & CARDIA_PT_HIST_MASK] = cond;

    /* --- stage 2: five-point derivative -----------------------------------
     * y[n] = (1/8)(x[n] + 2x[n-1] - 2x[n-3] - x[n-4])
     * This is the causal form of the paper's centred difference, so its output
     * is the derivative 2 samples ago. A derivative is used because the QRS is
     * defined by how FAST the signal moves, not how far: a tall, slow T wave
     * and a shorter, fast QRS have similar amplitudes but differ by an order
     * of magnitude in slope. */
    const float d = 0.125f * (bp
                              + 2.0f * pt->deriv_hist[0]
                              - 2.0f * pt->deriv_hist[2]
                              - pt->deriv_hist[3]);
    pt->deriv_hist[3] = pt->deriv_hist[2];
    pt->deriv_hist[2] = pt->deriv_hist[1];
    pt->deriv_hist[1] = pt->deriv_hist[0];
    pt->deriv_hist[0] = bp;

    /* --- stage 3: square --------------------------------------------------
     * Makes everything positive (so the integrator cannot cancel the QRS's own
     * up- and down-strokes against each other) and amplifies large slopes
     * quadratically, which widens the gap between QRS and everything else. */
    const float sq = d * d;

    /* --- stage 4: moving-window integration -------------------------------
     * A 150 ms rectangular window. The squared derivative of a QRS is two
     * narrow spikes (Q-R and R-S transitions); integrating merges them into
     * one hump whose width carries QRS duration information and whose single
     * maximum is easy to threshold. 150 ms is the standard choice: wide enough
     * to span the widest QRS, narrow enough not to swallow the T wave too. */
    pt->mwi_sum -= pt->mwi_ring[pt->mwi_pos];
    pt->mwi_ring[pt->mwi_pos] = sq;
    pt->mwi_sum += sq;
    pt->mwi_pos = (pt->mwi_pos + 1u) % (size_t)CARDIA_PT_INTEGRATION_SAMPLES;
    const float mwi = pt->mwi_sum / (float)CARDIA_PT_INTEGRATION_SAMPLES;

    /* --- learning phase ---------------------------------------------------
     * The paper initialises thresholds from the first two seconds. Without
     * this, the very first beats are either all missed (thresholds too high)
     * or the detector locks onto noise (too low). */
    if (pt->learning) {
        if (mwi > pt->learn_mwi_max) pt->learn_mwi_max = mwi;
        pt->learn_mwi_sum += mwi;
        const float abp = f_abs(bp);
        if (abp > pt->learn_bp_max) pt->learn_bp_max = abp;
        pt->learn_bp_sum += abp;

        if (n + 1u >= (uint32_t)CARDIA_PT_LEARNING_SAMPLES) {
            const float count = (float)CARDIA_PT_LEARNING_SAMPLES;
            pt->spki = pt->learn_mwi_max / 3.0f;
            pt->npki = (pt->learn_mwi_sum / count) / 2.0f;
            pt->spkf = pt->learn_bp_max / 3.0f;
            pt->npkf = (pt->learn_bp_sum / count) / 2.0f;
            pt->learning = 0;
        }
        pt->mwi_prev2 = pt->mwi_prev1;
        pt->mwi_prev1 = mwi;
        pt->n = n + 1u;
        return 0;
    }

    /* --- local maximum of the integrator ---------------------------------- */
    const int is_local_max = (pt->mwi_prev1 > pt->mwi_prev2) && (pt->mwi_prev1 >= mwi);
    const uint32_t peak_index = (n >= 1u) ? (n - 1u) : 0u;
    const float peak_mwi = pt->mwi_prev1;

    float thr_i1 = pt->npki + 0.25f * (pt->spki - pt->npki);
    float thr_f1 = pt->npkf + 0.25f * (pt->spkf - pt->npkf);
    if (rhythm_irregular(pt)) {
        thr_i1 *= 0.5f;
        thr_f1 *= 0.5f;
    }

    if (is_local_max && peak_index > (uint32_t)(CARDIA_PT_INTEGRATION_SAMPLES + 4)) {
        cardia_pt_candidate_t cand;
        refine_peak(pt, peak_index, &cand);
        cand.mwi = peak_mwi;

        const uint32_t since = pt->have_qrs && cand.index > pt->last_qrs
                                   ? (cand.index - pt->last_qrs)
                                   : 0u;

        int reject_refractory = pt->have_qrs && (since < (uint32_t)CARDIA_PT_REFRACTORY_SAMPLES);

        /* --- T-wave discrimination ---------------------------------------
         * A candidate arriving 200-360 ms after a confirmed QRS is suspicious:
         * that is exactly where the T wave lands. A tall T wave can clear the
         * amplitude threshold. What it cannot do is match the QRS's slope --
         * repolarisation is a slow, diffuse process, depolarisation is a fast
         * synchronised wavefront. So compare slopes: less than half the last
         * QRS's slope means this is a T wave, not a beat. */
        int reject_twave = 0;
        if (!reject_refractory && pt->have_qrs &&
            since < (uint32_t)CARDIA_PT_TWAVE_SAMPLES &&
            cand.slope < 0.5f * pt->last_qrs_slope) {
            reject_twave = 1;
        }

        PT_TRACE("cand n=%u idx=%u since=%u mwi=%.6f thr_i1=%.6f bp=%.6f "
                 "thr_f1=%.6f slope=%.6f last_slope=%.6f ratio=%.3f refr=%d tw=%d\n",
                 (unsigned)n, (unsigned)cand.index, (unsigned)since,
                 (double)peak_mwi, (double)thr_i1, (double)cand.bp,
                 (double)thr_f1, (double)cand.slope, (double)pt->last_qrs_slope,
                 (double)(pt->last_qrs_slope > 0.0f ? cand.slope / pt->last_qrs_slope : -1.0f),
                 reject_refractory, reject_twave);

        if (reject_refractory) {
            /* Physiologically impossible; not even worth updating noise
             * estimates with, since it is part of the beat we already found. */
        } else if (peak_mwi > thr_i1 && cand.bp > thr_f1 && !reject_twave) {
            accept_qrs(pt, &cand, 0, out, &n_out);
        } else {
            pt->npki = 0.125f * peak_mwi + 0.875f * pt->npki;
            pt->npkf = 0.125f * cand.bp + 0.875f * pt->npkf;
            if (pt->n_candidates < CARDIA_PT_MAX_CANDIDATES) {
                pt->candidates[pt->n_candidates++] = cand;
            }
        }
    }

    /* --- search-back ------------------------------------------------------
     * If no QRS has been accepted for 1.66x the expected interval, a beat was
     * almost certainly missed because the signal shrank. Re-examine the
     * candidates rejected since the last beat against a threshold half as
     * strict. Without this the detector silently drops beats during amplitude
     * dropouts -- and a missed beat corrupts two RR intervals, so it costs the
     * classifier two beats, not one. */
    if (pt->have_qrs && pt->n_candidates > 0u) {
        const uint32_t missed_limit =
            (uint32_t)(CARDIA_PT_SEARCHBACK_FACTOR * pt->rr_mean2);
        if (missed_limit > 0u && (n - pt->last_qrs) > missed_limit) {
            const float thr_i2 = 0.5f * thr_i1;
            const float thr_f2 = 0.5f * thr_f1;

            size_t best = (size_t)-1;
            float best_mwi = 0.0f;
            for (size_t i = 0; i < pt->n_candidates; ++i) {
                const cardia_pt_candidate_t *c = &pt->candidates[i];
                if (c->index <= pt->last_qrs) continue;
                if ((c->index - pt->last_qrs) < (uint32_t)CARDIA_PT_REFRACTORY_SAMPLES) continue;
                if (c->mwi <= thr_i2 || c->bp <= thr_f2) continue;
                if (c->mwi > best_mwi) {
                    best_mwi = c->mwi;
                    best = i;
                }
            }
            if (best != (size_t)-1) {
                cardia_pt_candidate_t c = pt->candidates[best];
                accept_qrs(pt, &c, 1, out, &n_out);
            }
        }
    }

    pt->mwi_prev2 = pt->mwi_prev1;
    pt->mwi_prev1 = mwi;
    pt->n = n + 1u;
    return n_out;
}
