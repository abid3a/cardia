/* pipeline.h -- the complete signal chain, portable C.
 *
 *   raw sample
 *     -> 0.5-40 Hz conditioning bandpass  (morphology for the classifier)
 *     -> Pan-Tompkins QRS detection       (its own 5-15 Hz band internally)
 *     -> beat window extraction + per-beat normalisation
 *     -> RR-interval features
 *     -> int8 CNN inference
 *     -> AAMI class
 *
 * This module owns no MCU peripheral and calls no MCU header. The firmware
 * feeds it ADC samples; the host simulator feeds it MIT-BIH samples. Same code,
 * which is the entire basis of the claim that the board runs the model that was
 * evaluated.
 *
 * One design consequence worth stating up front: a beat is classified one beat
 * LATE. Two of the four RR features (post-RR and the post/pre ratio) need the
 * following R peak, because the compensatory pause after a beat is what
 * separates a ventricular ectopic from a supraventricular one. That is a real
 * ~0.8 s latency, accepted deliberately, and the buffering below exists to
 * support it.
 */

#ifndef CARDIA_PIPELINE_H
#define CARDIA_PIPELINE_H

#include <stdint.h>

#include "cardia_config.h"
#include "filters.h"
#include "pan_tompkins.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sample ring must span: the pre-R context, the post-R window, and the
 * detector's reporting latency. Power of two so the wrap is a mask. */
#define CARDIA_RING_LEN 512
#define CARDIA_RING_MASK (CARDIA_RING_LEN - 1)

/* Intervals kept for the causal local-RR baseline. One more than the averaging
 * window because the average excludes the most recent interval. */
#define CARDIA_RR_HIST (CARDIA_RR_LOCAL_WINDOW + 2)

typedef struct {
    uint32_t r_index;                      /* sample index of the R peak */
    uint8_t  aami_class;                   /* 0=N 1=S 2=V 3=F 4=Q */
    int32_t  logits[CARDIA_N_CLASSES];     /* raw int32 accumulators */
    float    rr[CARDIA_N_RR_FEATURES];     /* the timing features used */
    float    beat[CARDIA_BEAT_LEN];        /* normalised window, for debugging */
} cardia_beat_result_t;

/* A beat waits on two independent events: its post-R window filling (156
 * samples, 433 ms) and the next R peak arriving to supply post-RR. At 200 bpm
 * the next R arrives after only ~108 samples, so several beats can legitimately
 * be in flight at once. Four slots, statically allocated, no dynamic memory. */
#define CARDIA_PENDING_MAX 4

typedef struct {
    uint32_t r_index;
    float    pre_rr;      /* seconds */
    float    post_rr;     /* seconds */
    float    local_rr;    /* seconds, causal mean of earlier intervals */
    int      has_pre;
    int      has_post;
    int      window_ready; /* 0 = not yet, 1 = filled, -1 = unusable edge beat */
    float    beat[CARDIA_BEAT_LEN];
} cardia_pending_beat_t;

typedef struct {
    cardia_bandpass_t cond;
    cardia_pt_t pt;

    float ring[CARDIA_RING_LEN];
    uint32_t n;                    /* total samples consumed */

    cardia_pending_beat_t pending[CARDIA_PENDING_MAX];
    int n_pending;

    float rr_hist[CARDIA_RR_HIST];   /* recent intervals, seconds, newest last */
    int   rr_count;

    uint32_t prev_r;
    int      have_prev_r;

    uint32_t beats_classified;
} cardia_pipeline_t;

typedef struct {
    int n_r;                                   /* R peaks detected this step */
    uint32_t r_index[CARDIA_PT_MAX_EMIT];
    int have_result;                           /* 1 if `result` is populated */
    cardia_beat_result_t result;
} cardia_pipeline_out_t;

void cardia_pipeline_init(cardia_pipeline_t *p);

/* Feed one raw sample. Fills `out`. Returns 1 if a beat was classified. */
int cardia_pipeline_step(cardia_pipeline_t *p, float raw, cardia_pipeline_out_t *out);

/* Exposed for unit tests and for the firmware's debug mode. */
void cardia_normalize_beat(const float *in, float *out, int32_t len);
void cardia_rr_features(float pre_rr, float post_rr, float local_rr,
                        float out[CARDIA_N_RR_FEATURES]);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_PIPELINE_H */
