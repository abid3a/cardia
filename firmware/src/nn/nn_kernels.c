#include "nn_kernels.h"

void cardia_conv1d_s8(const int8_t *in, int32_t in_len, int32_t in_ch,
                      const int8_t *weights, const int32_t *bias,
                      const int32_t *mult, const int32_t *shift,
                      int32_t out_ch, int32_t kernel, int32_t stride, int32_t pad,
                      int32_t input_offset, int32_t output_offset,
                      int32_t act_min, int32_t act_max,
                      int8_t *out, int32_t out_len)
{
    for (int32_t o = 0; o < out_len; ++o) {
        const int32_t base = o * stride - pad;
        for (int32_t oc = 0; oc < out_ch; ++oc) {
            int32_t acc = bias ? bias[oc] : 0;
            const int8_t *w = weights + (int32_t)oc * kernel * in_ch;

            for (int32_t k = 0; k < kernel; ++k) {
                const int32_t idx = base + k;
                /* Zero padding is padding with the *quantised zero*, which is
                 * -input_offset in int8 space, not the literal byte 0. Getting
                 * this wrong is the single most common int8 conv bug: it
                 * silently injects a DC step at both edges of every window. */
                if (idx < 0 || idx >= in_len) continue;
                const int8_t *x = in + (int32_t)idx * in_ch;
                const int8_t *wk = w + (int32_t)k * in_ch;
                for (int32_t c = 0; c < in_ch; ++c) {
                    acc += (int32_t)wk[c] * ((int32_t)x[c] + input_offset);
                }
            }

            int32_t v = cardia_requantize(acc, mult[oc], shift[oc]);
            v += output_offset;
            if (v < act_min) v = act_min;
            if (v > act_max) v = act_max;
            out[(int32_t)o * out_ch + oc] = (int8_t)v;
        }
    }
}

void cardia_maxpool1d_s8(const int8_t *in, int32_t in_len, int32_t ch,
                         int32_t kernel, int8_t *out, int32_t out_len)
{
    (void)in_len;
    for (int32_t o = 0; o < out_len; ++o) {
        for (int32_t c = 0; c < ch; ++c) {
            int8_t best = in[((int32_t)o * kernel) * ch + c];
            for (int32_t k = 1; k < kernel; ++k) {
                const int8_t v = in[((int32_t)o * kernel + k) * ch + c];
                if (v > best) best = v;
            }
            out[(int32_t)o * ch + c] = best;
        }
    }
}

void cardia_global_avgpool_s8(const int8_t *in, int32_t in_len, int32_t ch,
                              int32_t input_offset, int32_t output_offset,
                              int32_t mult, int32_t shift,
                              int32_t act_min, int32_t act_max, int8_t *out)
{
    for (int32_t c = 0; c < ch; ++c) {
        int32_t acc = 0;
        for (int32_t i = 0; i < in_len; ++i) {
            acc += (int32_t)in[(int32_t)i * ch + c] + input_offset;
        }
        /* The division by in_len is folded into `mult`/`shift` by the exporter
         * rather than done here, so there is exactly one rounding step. */
        int32_t v = cardia_requantize(acc, mult, shift) + output_offset;
        if (v < act_min) v = act_min;
        if (v > act_max) v = act_max;
        out[c] = (int8_t)v;
    }
}

void cardia_fully_connected_s8(const int8_t *in, int32_t in_len,
                               const int8_t *weights, const int32_t *bias,
                               int32_t out_len,
                               int32_t input_offset, int32_t output_offset,
                               int32_t mult, int32_t shift,
                               int32_t act_min, int32_t act_max, int8_t *out)
{
    for (int32_t o = 0; o < out_len; ++o) {
        int32_t acc = bias ? bias[o] : 0;
        const int8_t *w = weights + (int32_t)o * in_len;
        for (int32_t i = 0; i < in_len; ++i) {
            acc += (int32_t)w[i] * ((int32_t)in[i] + input_offset);
        }
        int32_t v = cardia_requantize(acc, mult, shift) + output_offset;
        if (v < act_min) v = act_min;
        if (v > act_max) v = act_max;
        out[o] = (int8_t)v;
    }
}

void cardia_fully_connected_s32(const int8_t *in, int32_t in_len,
                                const int8_t *weights, const int32_t *bias,
                                int32_t out_len, int32_t input_offset,
                                int32_t *out)
{
    for (int32_t o = 0; o < out_len; ++o) {
        int32_t acc = bias ? bias[o] : 0;
        const int8_t *w = weights + (int32_t)o * in_len;
        for (int32_t i = 0; i < in_len; ++i) {
            acc += (int32_t)w[i] * ((int32_t)in[i] + input_offset);
        }
        out[o] = acc;
    }
}

void cardia_quantize_f32(const float *in, int32_t len, float scale,
                         int32_t zero_point, int8_t *out)
{
    const float inv = 1.0f / scale;
    for (int32_t i = 0; i < len; ++i) {
        /* Round half away from zero, matching numpy's rint-free
         * `np.floor(x + 0.5)` convention used by the exporter's checker. */
        const float v = in[i] * inv;
        const int32_t q = (int32_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f)) + zero_point;
        out[i] = cardia_sat_int8(q);
    }
}
