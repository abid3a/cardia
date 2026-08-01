#include "pipeline.h"
#include "../nn/inference.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Beat normalisation and RR features                                         */
/* ------------------------------------------------------------------------- */

void cardia_normalize_beat(const float *in, float *out, int32_t len)
{
    float sum = 0.0f;
    for (int32_t i = 0; i < len; ++i) sum += in[i];
    const float mean = sum / (float)len;

    float var = 0.0f;
    for (int32_t i = 0; i < len; ++i) {
        const float d = in[i] - mean;
        var += d * d;
    }
    /* Population standard deviation, matching numpy's default ddof=0. Using
     * the sample form (ddof=1) here would be a silent 0.2% scale mismatch
     * against the training data -- small, but it is exactly the kind of thing
     * that turns a bit-exact parity test into a "close enough" one. */
    const float std = __builtin_sqrtf(var / (float)len);

    const float inv = 1.0f / (std + 1e-6f);
    for (int32_t i = 0; i < len; ++i) out[i] = (in[i] - mean) * inv;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void cardia_rr_features(float pre_rr, float post_rr, float local_rr,
                        float out[CARDIA_N_RR_FEATURES])
{
    const float c = CARDIA_RR_RATIO_CLIP;
    out[0] = clampf(pre_rr / c, -1.0f, 1.0f);
    out[1] = clampf(post_rr / c, -1.0f, 1.0f);
    out[2] = clampf(pre_rr / (local_rr + 1e-6f), -c, c) / c;
    out[3] = clampf(post_rr / (pre_rr + 1e-6f), -c, c) / c;
}

/* ------------------------------------------------------------------------- */
/* Pending-beat queue                                                         */
/* ------------------------------------------------------------------------- */

typedef cardia_pending_beat_t pending_t;

void cardia_pipeline_init(cardia_pipeline_t *p)
{
    memset(p, 0, sizeof(*p));
    cardia_bandpass_init(&p->cond);
    cardia_pt_init(&p->pt);
}

static void rr_push(cardia_pipeline_t *p, float interval)
{
    if (p->rr_count < CARDIA_RR_HIST) {
        p->rr_hist[p->rr_count++] = interval;
    } else {
        for (int i = 1; i < CARDIA_RR_HIST; ++i) p->rr_hist[i - 1] = p->rr_hist[i];
        p->rr_hist[CARDIA_RR_HIST - 1] = interval;
    }
}

static void extract_window(const cardia_pipeline_t *p, uint32_t r, float *dst)
{
    const uint32_t start = r - (uint32_t)CARDIA_BEAT_PRE;
    for (int32_t i = 0; i < CARDIA_BEAT_LEN; ++i) {
        dst[i] = p->ring[(start + (uint32_t)i) & CARDIA_RING_MASK];
    }
}

int cardia_pipeline_step(cardia_pipeline_t *p, float raw, cardia_pipeline_out_t *out)
{
    out->n_r = 0;
    out->have_result = 0;

    const uint32_t n = p->n;

    /* --- conditioning path ------------------------------------------------ */
    const float cond = cardia_bandpass_step(&p->cond, raw);
    p->ring[n & CARDIA_RING_MASK] = cond;

    /* --- detection path --------------------------------------------------- */
    uint32_t r_peaks[CARDIA_PT_MAX_EMIT];
    const int n_r = cardia_pt_step(&p->pt, raw, cond, r_peaks);
    out->n_r = n_r;
    for (int i = 0; i < n_r; ++i) out->r_index[i] = r_peaks[i];

    pending_t *q = (pending_t *)p->pending;

    for (int i = 0; i < n_r; ++i) {
        const uint32_t r = r_peaks[i];

        /* The new R peak closes the previous beat's post-RR interval. */
        if (p->have_prev_r && r > p->prev_r) {
            const float interval = (float)(r - p->prev_r) / (float)CARDIA_FS_HZ;
            for (int k = 0; k < p->n_pending; ++k) {
                if (q[k].r_index == p->prev_r && !q[k].has_post) {
                    q[k].post_rr = interval;
                    q[k].has_post = 1;
                    /* Beat 0 has no earlier R peak. The reference
                     * implementation substitutes the first interval for the
                     * missing pre-RR, so do the same rather than inventing a
                     * different convention. */
                    if (!q[k].has_pre) {
                        q[k].pre_rr = interval;
                        q[k].local_rr = interval;
                        q[k].has_pre = 1;
                    }
                }
            }
            rr_push(p, interval);
        }

        if (p->n_pending < CARDIA_PENDING_MAX) {
            pending_t *slot = &q[p->n_pending++];
            memset(slot, 0, sizeof(*slot));
            slot->r_index = r;
            if (p->have_prev_r && r > p->prev_r) {
                slot->pre_rr = (float)(r - p->prev_r) / (float)CARDIA_FS_HZ;
                slot->has_pre = 1;
                /* local_rr_mean() is read AFTER rr_push above, so the history
                 * now ends with this beat's own pre-RR. Skip it by averaging
                 * the entries before it. */
                const int keep = p->rr_count - 1;
                float sum = 0.0f;
                int cnt = 0;
                const int w = CARDIA_RR_LOCAL_WINDOW;
                for (int k = keep - 1; k >= 0 && cnt < w; --k, ++cnt) sum += p->rr_hist[k];
                slot->local_rr = (cnt > 0) ? (sum / (float)cnt) : slot->pre_rr;
            }
        }

        p->prev_r = r;
        p->have_prev_r = 1;
    }

    /* --- fill windows for pending beats ----------------------------------- */
    for (int k = 0; k < p->n_pending; ++k) {
        if (q[k].window_ready) continue;
        if (q[k].r_index < (uint32_t)CARDIA_BEAT_PRE) {
            /* Too close to the start of the record for a full window; the
             * Python reference drops these beats rather than padding, so drop
             * them here too. */
            q[k].window_ready = -1;
            continue;
        }
        if (n + 1u >= q[k].r_index + (uint32_t)CARDIA_BEAT_POST) {
            extract_window(p, q[k].r_index, q[k].beat);
            q[k].window_ready = 1;
        }
    }

    /* --- emit the oldest fully-resolved beat ------------------------------ */
    int emitted = 0;
    int drop = -1;
    for (int k = 0; k < p->n_pending; ++k) {
        if (q[k].window_ready == -1 && q[k].has_post) { drop = k; break; }
        if (q[k].window_ready == 1 && q[k].has_post && q[k].has_pre) {
            cardia_beat_result_t *res = &out->result;
            res->r_index = q[k].r_index;
            cardia_normalize_beat(q[k].beat, res->beat, CARDIA_BEAT_LEN);
            cardia_rr_features(q[k].pre_rr, q[k].post_rr, q[k].local_rr, res->rr);
            res->aami_class = cardia_classify(res->beat, res->rr, res->logits);
            out->have_result = 1;
            p->beats_classified++;
            emitted = 1;
            drop = k;
            break;
        }
    }

    if (drop >= 0) {
        for (int k = drop; k < p->n_pending - 1; ++k) q[k] = q[k + 1];
        p->n_pending--;
    }

    p->n = n + 1u;
    return emitted;
}
