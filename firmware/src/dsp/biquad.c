#include "biquad.h"

void cardia_biquad_init(cardia_biquad_cascade_t *c,
                        const cardia_biquad_coeffs_t *sections,
                        cardia_biquad_state_t *states,
                        size_t n_sections)
{
    c->sections = sections;
    c->states = states;
    c->n_sections = n_sections;
    cardia_biquad_reset(c);
}

void cardia_biquad_reset(cardia_biquad_cascade_t *c)
{
    for (size_t i = 0; i < c->n_sections; ++i) {
        c->states[i].x1 = 0.0f;
        c->states[i].x2 = 0.0f;
        c->states[i].y1 = 0.0f;
        c->states[i].y2 = 0.0f;
    }
}

float cardia_biquad_step(cardia_biquad_cascade_t *c, float x)
{
    for (size_t i = 0; i < c->n_sections; ++i) {
        const cardia_biquad_coeffs_t *k = &c->sections[i];
        cardia_biquad_state_t *s = &c->states[i];

        /* Direct form I difference equation. On Cortex-M4F each of these five
         * products is a single-cycle VFMA on the hardware FPU, so the whole
         * section is ~10 cycles including the state shuffle. Fixed-point would
         * be no faster here and would cost accuracy. */
        const float y = k->b0 * x
                      + k->b1 * s->x1
                      + k->b2 * s->x2
                      - k->a1 * s->y1
                      - k->a2 * s->y2;

        s->x2 = s->x1;
        s->x1 = x;
        s->y2 = s->y1;
        s->y1 = y;

        x = y; /* cascade: this section's output feeds the next */
    }
    return x;
}

void cardia_biquad_block(cardia_biquad_cascade_t *c,
                         const float *src, float *dst, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        dst[i] = cardia_biquad_step(c, src[i]);
    }
}
