/* filters.h -- the project's fixed ECG conditioning filter. */

#ifndef CARDIA_FILTERS_H
#define CARDIA_FILTERS_H

#include "biquad.h"
#include "cardia_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0.5-40 Hz conditioning band: feeds the classifier, preserves morphology. */
extern const cardia_biquad_coeffs_t cardia_bandpass_sos[CARDIA_BP_SECTIONS];

/* 5-15 Hz detection band: feeds Pan-Tompkins, suppresses P and T on purpose.
 * Two filters run in parallel on the same raw stream because they have
 * opposite jobs -- see the note in ml/cardia/config.py. */
extern const cardia_biquad_coeffs_t cardia_qrs_bandpass_sos[CARDIA_QRS_BP_SECTIONS];

/* Convenience wrappers bundling coefficients with caller-owned state. */
typedef struct {
    cardia_biquad_cascade_t cascade;
    cardia_biquad_state_t states[CARDIA_BP_SECTIONS];
} cardia_bandpass_t;

typedef struct {
    cardia_biquad_cascade_t cascade;
    cardia_biquad_state_t states[CARDIA_QRS_BP_SECTIONS];
} cardia_qrs_bandpass_t;

static inline void cardia_bandpass_init(cardia_bandpass_t *f)
{
    cardia_biquad_init(&f->cascade, cardia_bandpass_sos, f->states,
                       CARDIA_BP_SECTIONS);
}

static inline float cardia_bandpass_step(cardia_bandpass_t *f, float x)
{
    return cardia_biquad_step(&f->cascade, x);
}

static inline void cardia_qrs_bandpass_init(cardia_qrs_bandpass_t *f)
{
    cardia_biquad_init(&f->cascade, cardia_qrs_bandpass_sos, f->states,
                       CARDIA_QRS_BP_SECTIONS);
}

static inline float cardia_qrs_bandpass_step(cardia_qrs_bandpass_t *f, float x)
{
    return cardia_biquad_step(&f->cascade, x);
}

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_FILTERS_H */
