/* biquad.h -- cascaded second-order IIR sections, direct form I.
 *
 * Portable C. No MCU headers, no dynamic allocation, no libm. Compiles for the
 * host and for Cortex-M4F from this one file, which is what makes the
 * host-vs-target parity check in sim/ meaningful.
 */

#ifndef CARDIA_BIQUAD_H
#define CARDIA_BIQUAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One second-order section in scipy's `sos` row layout:
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 * a0 is normalised to 1 by the filter designer, so it is not stored. */
typedef struct {
    float b0, b1, b2;
    float a1, a2;
} cardia_biquad_coeffs_t;

/* Per-section delay line.
 *
 * Direct form I keeps input and output history separately: four state words per
 * section instead of two. The extra 8 bytes buy numerical robustness -- the
 * numerator and denominator do not share a summing node, so the large
 * intermediate value that a high-Q section produces cannot blow up the state
 * the way it can in direct form II. At 0.5 Hz on a 360 Hz sample rate the
 * high-pass section has its poles at radius ~0.9956, which is high-Q enough
 * that this is a practical concern in float32, not a textbook one. */
typedef struct {
    float x1, x2;
    float y1, y2;
} cardia_biquad_state_t;

typedef struct {
    const cardia_biquad_coeffs_t *sections;
    cardia_biquad_state_t *states;
    size_t n_sections;
} cardia_biquad_cascade_t;

void cardia_biquad_init(cardia_biquad_cascade_t *c,
                        const cardia_biquad_coeffs_t *sections,
                        cardia_biquad_state_t *states,
                        size_t n_sections);

void cardia_biquad_reset(cardia_biquad_cascade_t *c);

/* Process one sample through the whole cascade. */
float cardia_biquad_step(cardia_biquad_cascade_t *c, float x);

/* Process a block. `dst` may alias `src`. */
void cardia_biquad_block(cardia_biquad_cascade_t *c,
                         const float *src, float *dst, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_BIQUAD_H */
