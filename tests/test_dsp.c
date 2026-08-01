/* Unit tests for the portable DSP layer.
 *
 * Expected values come from tests/test_vectors.h, which is generated from the
 * Python/scipy reference. These are therefore cross-implementation tests, not
 * change-detector tests.
 */

#include "test_harness.h"
#include "test_vectors.h"

#include "biquad.h"
#include "filters.h"
#include "pipeline.h"
#include "pan_tompkins.h"

#include <string.h>

/* float32 accumulates differently from the float64 scipy uses, so an exact
 * comparison would be testing the FPU, not the filter. 1e-6 absolute on an
 * impulse response whose peak is ~0.3 is roughly 20 bits of agreement. */
#define FILT_TOL 1e-6

TEST(bandpass_matches_scipy_impulse_response)
{
    cardia_bandpass_t f;
    cardia_bandpass_init(&f);
    for (int i = 0; i < TV_IMPULSE_LEN; ++i) {
        const float x = (i == 0) ? 1.0f : 0.0f;
        CHECK_NEAR(cardia_bandpass_step(&f, x), tv_bandpass_impulse[i], FILT_TOL);
    }
}

TEST(qrs_bandpass_matches_scipy_impulse_response)
{
    cardia_qrs_bandpass_t f;
    cardia_qrs_bandpass_init(&f);
    for (int i = 0; i < TV_IMPULSE_LEN; ++i) {
        const float x = (i == 0) ? 1.0f : 0.0f;
        CHECK_NEAR(cardia_qrs_bandpass_step(&f, x), tv_qrs_bandpass_impulse[i], FILT_TOL);
    }
}

TEST(bandpass_rejects_dc)
{
    /* A 0.5 Hz high-pass must drive a constant input to zero. If it does not,
     * baseline wander survives into the beat window and every normalisation
     * downstream is measuring electrode drift instead of cardiac activity. */
    cardia_bandpass_t f;
    cardia_bandpass_init(&f);
    float y = 0.0f;
    for (int i = 0; i < 4000; ++i) y = cardia_bandpass_step(&f, 1.0f);
    CHECK_NEAR(y, 0.0f, 1e-3);
}

TEST(bandpass_passes_10hz)
{
    /* 10 Hz sits in the middle of the QRS band and must come through with
     * close to unity gain. */
    cardia_bandpass_t f;
    cardia_bandpass_init(&f);
    float peak = 0.0f;
    const float w = 2.0f * 3.14159265358979f * 10.0f / (float)CARDIA_FS_HZ;
    for (int i = 0; i < 4000; ++i) {
        float s = 0.0f;
        /* cheap sine without libm: recurrence-free, just sample a table-free
         * polynomial approximation is overkill -- use the exact call, the host
         * has libm and this is a test, not firmware. */
        s = __builtin_sinf(w * (float)i);
        const float y = cardia_bandpass_step(&f, s);
        if (i > 2000 && y > peak) peak = y;
    }
    CHECK(peak > 0.9f);
    CHECK(peak < 1.1f);
}

TEST(bandpass_attenuates_60hz)
{
    cardia_bandpass_t f;
    cardia_bandpass_init(&f);
    float peak = 0.0f;
    const float w = 2.0f * 3.14159265358979f * 60.0f / (float)CARDIA_FS_HZ;
    for (int i = 0; i < 4000; ++i) {
        const float y = cardia_bandpass_step(&f, __builtin_sinf(w * (float)i));
        if (i > 2000 && y > peak) peak = y;
    }
    /* 60 Hz mains hum is well past the 40 Hz corner of a 4th-order filter. */
    CHECK(peak < 0.35f);
}

TEST(biquad_state_reset_is_clean)
{
    cardia_bandpass_t f;
    cardia_bandpass_init(&f);
    for (int i = 0; i < 100; ++i) (void)cardia_bandpass_step(&f, 3.0f);
    cardia_biquad_reset(&f.cascade);
    const float first = cardia_bandpass_step(&f, 1.0f);
    CHECK_NEAR(first, tv_bandpass_impulse[0], FILT_TOL);
}

TEST(normalize_beat_matches_reference)
{
    float out[TV_BEAT_LEN];
    cardia_normalize_beat(tv_norm_input, out, TV_BEAT_LEN);
    for (int i = 0; i < TV_BEAT_LEN; ++i) {
        CHECK_NEAR(out[i], tv_norm_expected[i], 1e-5);
    }
}

TEST(normalize_beat_is_zero_mean_unit_std)
{
    float out[TV_BEAT_LEN];
    cardia_normalize_beat(tv_norm_input, out, TV_BEAT_LEN);
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < TV_BEAT_LEN; ++i) { sum += out[i]; sq += (double)out[i] * out[i]; }
    const double mean = sum / TV_BEAT_LEN;
    CHECK_NEAR(mean, 0.0, 1e-4);
    CHECK_NEAR(sq / TV_BEAT_LEN - mean * mean, 1.0, 1e-3);
}

TEST(normalize_beat_survives_flat_input)
{
    /* A dead lead gives a constant window; std is 0 and the epsilon in the
     * divisor is the only thing standing between this and a NaN propagating
     * into the classifier. */
    float in[TV_BEAT_LEN], out[TV_BEAT_LEN];
    for (int i = 0; i < TV_BEAT_LEN; ++i) in[i] = 2.5f;
    cardia_normalize_beat(in, out, TV_BEAT_LEN);
    for (int i = 0; i < TV_BEAT_LEN; ++i) CHECK(out[i] == out[i]); /* not NaN */
}

TEST(rr_features_match_reference)
{
    float out[CARDIA_N_RR_FEATURES];
    cardia_rr_features(TV_RR_PRE, TV_RR_POST, TV_RR_LOCAL, out);
    for (int i = 0; i < CARDIA_N_RR_FEATURES; ++i) {
        CHECK_NEAR(out[i], tv_rr_expected[i], 1e-6);
    }
}

TEST(rr_features_flag_prematurity)
{
    /* A beat arriving at 60% of the local interval must produce a pre/local
     * ratio well below the normal beat's. This is the feature that carries
     * class S, so a regression here would be invisible in accuracy but fatal
     * to the only class that depends on timing. */
    float normal[CARDIA_N_RR_FEATURES], early[CARDIA_N_RR_FEATURES];
    cardia_rr_features(0.80f, 0.80f, 0.80f, normal);
    cardia_rr_features(0.48f, 1.12f, 0.80f, early);
    CHECK(early[2] < normal[2]);
    CHECK(early[3] > normal[3]); /* compensatory pause */
}

TEST(pan_tompkins_finds_synthetic_beats)
{
    static cardia_pt_t pt;
    static cardia_bandpass_t cond;
    cardia_pt_init(&pt);
    cardia_bandpass_init(&cond);

    uint32_t found[64];
    int n_found = 0;
    for (int i = 0; i < TV_SYNTH_LEN; ++i) {
        const float c = cardia_bandpass_step(&cond, tv_synth_ecg[i]);
        uint32_t out[CARDIA_PT_MAX_EMIT];
        const int n = cardia_pt_step(&pt, tv_synth_ecg[i], c, out);
        for (int k = 0; k < n && n_found < 64; ++k) found[n_found++] = out[k];
    }

    /* The first 2 s are the detector's threshold-learning phase, so beats in
     * that window are legitimately not reported. Score only what comes after. */
    int expected = 0;
    for (int i = 0; i < TV_SYNTH_NBEATS; ++i) {
        if (tv_synth_r_peaks[i] > 2 * CARDIA_FS_HZ) expected++;
    }

    int matched = 0;
    for (int i = 0; i < TV_SYNTH_NBEATS; ++i) {
        if (tv_synth_r_peaks[i] <= 2 * CARDIA_FS_HZ) continue;
        for (int k = 0; k < n_found; ++k) {
            const long d = (long)found[k] - (long)tv_synth_r_peaks[i];
            if (d > -CARDIA_QRS_MATCH_TOL_SAMPLES && d < CARDIA_QRS_MATCH_TOL_SAMPLES) {
                matched++;
                break;
            }
        }
    }
    printf("    detected %d peaks, matched %d of %d post-learning beats\n",
           n_found, matched, expected);
    CHECK_INT_EQ(matched, expected);
    /* No spurious extras: at most one detection per real beat plus whatever
     * the learning phase emitted. */
    CHECK(n_found <= TV_SYNTH_NBEATS);
}

TEST(pan_tompkins_rejects_tall_t_waves)
{
    /* Synthesise a rhythm whose T wave is nearly as tall as the R wave. Pure
     * amplitude thresholding would double-count every beat and report twice
     * the true heart rate -- the classic Pan-Tompkins failure this test pins
     * down. The slope check is what saves it. */
    static cardia_pt_t pt;
    static cardia_bandpass_t cond;
    cardia_pt_init(&pt);
    cardia_bandpass_init(&cond);

    const int n = 360 * 20;
    const float period = 0.8f;      /* 75 bpm */
    const float r_offset = 0.10f;   /* R peak position within each period */
    int n_found = 0;

    for (int i = 0; i < n; ++i) {
        const float t = (float)i / (float)CARDIA_FS_HZ;
        const int k = (int)(t / period);
        const float phase = t - (float)k * period;
        float x = 0.0f;
        /* narrow, fast R */
        float d = phase - r_offset;
        x += 1.0f * __builtin_expf(-0.5f * (d / 0.008f) * (d / 0.008f));
        /* broad, slow T at 80% of the R amplitude */
        d = phase - 0.32f;
        x += 0.80f * __builtin_expf(-0.5f * (d / 0.050f) * (d / 0.050f));

        const float c = cardia_bandpass_step(&cond, x);
        uint32_t out[CARDIA_PT_MAX_EMIT];
        n_found += cardia_pt_step(&pt, x, c, out);
    }

    /* Count R peaks analytically rather than by watching the phase wrap: the
     * detector reports nothing during its first two seconds of threshold
     * learning, so only beats after t = 2 s are expected. */
    int expected_beats = 0;
    for (int k = 0;; ++k) {
        const float t = r_offset + (float)k * period;
        if (t >= (float)n / (float)CARDIA_FS_HZ) break;
        if (t > 2.0f) expected_beats++;
    }

    printf("    %d detections for %d beats (T wave at 80%% of R)\n",
           n_found, expected_beats);
    /* The failure this pins down is double-counting: if the slope-based T-wave
     * rule were removed, the tall T would clear the amplitude threshold and
     * the reported heart rate would be exactly twice the truth. */
    CHECK(n_found <= expected_beats + 1);
    CHECK(n_found >= expected_beats - 1);
}

TEST(pipeline_classifies_synthetic_stream)
{
    static cardia_pipeline_t p;
    cardia_pipeline_init(&p);
    cardia_pipeline_out_t out;
    int beats = 0, detections = 0;
    for (int i = 0; i < TV_SYNTH_LEN; ++i) {
        if (cardia_pipeline_step(&p, tv_synth_ecg[i], &out)) beats++;
        detections += out.n_r;
    }
    printf("    pipeline: %d R peaks, %d beats classified\n", detections, beats);
    /* Every classified beat needs its window AND the following R peak, so the
     * count trails the detection count by one or two. */
    CHECK(beats > 0);
    CHECK(beats <= detections);
    CHECK(detections - beats <= 3);
}

int main(void)
{
    printf("test_dsp\n");
    RUN_TEST(bandpass_matches_scipy_impulse_response);
    RUN_TEST(qrs_bandpass_matches_scipy_impulse_response);
    RUN_TEST(bandpass_rejects_dc);
    RUN_TEST(bandpass_passes_10hz);
    RUN_TEST(bandpass_attenuates_60hz);
    RUN_TEST(biquad_state_reset_is_clean);
    RUN_TEST(normalize_beat_matches_reference);
    RUN_TEST(normalize_beat_is_zero_mean_unit_std);
    RUN_TEST(normalize_beat_survives_flat_input);
    RUN_TEST(rr_features_match_reference);
    RUN_TEST(rr_features_flag_prematurity);
    RUN_TEST(pan_tompkins_finds_synthetic_beats);
    RUN_TEST(pan_tompkins_rejects_tall_t_waves);
    RUN_TEST(pipeline_classifies_synthetic_stream);
    return test_report("test_dsp");
}
