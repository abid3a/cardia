/* nn_kernels.h -- portable int8 neural-network primitives.
 *
 * These are the reference kernels: plain C, no intrinsics, no CMSIS. They run
 * on the host so the simulator can execute the *exact* network the MCU runs,
 * and they run on the target as a fallback when CMSIS-NN is not linked in.
 *
 * The arithmetic deliberately mirrors the gemmlowp / TFLite / CMSIS-NN
 * requantisation scheme bit for bit -- saturating doubling high multiply,
 * then a rounding divide by a power of two. That is not an aesthetic choice:
 * it means swapping in CMSIS-NN's SIMD kernels produces *identical* integer
 * outputs, not merely similar ones, so "does the firmware match the model"
 * has a yes/no answer instead of a tolerance.
 */

#ifndef CARDIA_NN_KERNELS_H
#define CARDIA_NN_KERNELS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Requantisation ------------------------------------------------------ */

/* Saturating rounding doubling high multiply: (a * b) >> 31 with rounding,
 * saturating the single degenerate case a == b == INT32_MIN. */
static inline int32_t cardia_sat_doubling_high_mul(int32_t a, int32_t b)
{
    if (a == (int32_t)0x80000000 && b == (int32_t)0x80000000) {
        return (int32_t)0x7FFFFFFF;
    }
    int64_t prod = (int64_t)a * (int64_t)b;
    int32_t nudge = (prod >= 0) ? (1 << 30) : (1 - (1 << 30));
    return (int32_t)((prod + nudge) / (1LL << 31));
}

/* Rounding divide by 2^exponent, round-half-away-from-zero. */
static inline int32_t cardia_rounding_div_pot(int32_t x, int32_t exponent)
{
    if (exponent <= 0) return x;
    const int32_t mask = (1 << exponent) - 1;
    const int32_t remainder = x & mask;
    const int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
    return (x >> exponent) + ((remainder > threshold) ? 1 : 0);
}

/* acc * (multiplier * 2^(shift-31)), i.e. the fixed-point form of a real
 * scale factor. `shift` is positive for a left shift, negative for right. */
static inline int32_t cardia_requantize(int32_t acc, int32_t multiplier, int32_t shift)
{
    const int32_t left = (shift > 0) ? shift : 0;
    const int32_t right = (shift > 0) ? 0 : -shift;
    return cardia_rounding_div_pot(
        cardia_sat_doubling_high_mul(acc * (1 << left), multiplier), right);
}

static inline int8_t cardia_sat_int8(int32_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

/* --- Layers --------------------------------------------------------------
 * Tensor layout is channels-last (NWC): `in[w * in_ch + c]`. This matches
 * CMSIS-NN and TFLite Micro, and it is the layout that makes a convolution's
 * inner loop a contiguous dot product over input channels -- which is exactly
 * what the Cortex-M4F SMLAD instruction wants.
 */

/* 1-D convolution, int8 in / int8 out, per-output-channel requantisation.
 *
 *   in        [in_len * in_ch]
 *   weights   [out_ch * kernel * in_ch]   (output-major, then tap, then channel)
 *   bias      [out_ch]                    int32, already in accumulator scale
 *   mult/shift[out_ch]                    per-channel requantisation
 *   out       [out_len * out_ch]
 *
 * Weights are symmetric (zero-point 0), activations are affine. That split is
 * the standard one: a symmetric weight zero-point removes an entire
 * cross-term from the accumulation, and weights are naturally near-zero-mean
 * anyway, so nothing is lost.
 */
void cardia_conv1d_s8(const int8_t *in, int32_t in_len, int32_t in_ch,
                      const int8_t *weights, const int32_t *bias,
                      const int32_t *mult, const int32_t *shift,
                      int32_t out_ch, int32_t kernel, int32_t stride, int32_t pad,
                      int32_t input_offset, int32_t output_offset,
                      int32_t act_min, int32_t act_max,
                      int8_t *out, int32_t out_len);

/* Max pooling, int8, stride == kernel (non-overlapping). */
void cardia_maxpool1d_s8(const int8_t *in, int32_t in_len, int32_t ch,
                         int32_t kernel, int8_t *out, int32_t out_len);

/* Global average pooling over the length axis, int8 -> int8. */
void cardia_global_avgpool_s8(const int8_t *in, int32_t in_len, int32_t ch,
                              int32_t input_offset, int32_t output_offset,
                              int32_t mult, int32_t shift,
                              int32_t act_min, int32_t act_max, int8_t *out);

/* Fully connected, int8 in / int8 out, per-tensor requantisation. */
void cardia_fully_connected_s8(const int8_t *in, int32_t in_len,
                               const int8_t *weights, const int32_t *bias,
                               int32_t out_len,
                               int32_t input_offset, int32_t output_offset,
                               int32_t mult, int32_t shift,
                               int32_t act_min, int32_t act_max, int8_t *out);

/* Fully connected producing raw int32 accumulators, for the final logit layer
 * where quantising the output would throw away the margin between classes. */
void cardia_fully_connected_s32(const int8_t *in, int32_t in_len,
                                const int8_t *weights, const int32_t *bias,
                                int32_t out_len, int32_t input_offset,
                                int32_t *out);

/* Quantise a float vector with a fixed scale/zero-point. */
void cardia_quantize_f32(const float *in, int32_t len, float scale,
                         int32_t zero_point, int8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_NN_KERNELS_H */
