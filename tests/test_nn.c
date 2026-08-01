/* Unit tests for the int8 kernels.
 *
 * These check the fixed-point arithmetic against values worked out by hand,
 * because the requantisation path is where a quantised network silently goes
 * wrong: an off-by-one in a rounding rule does not crash, it just shifts a few
 * percent of predictions, which looks like "the model is a bit worse" rather
 * than a bug.
 */

#include "test_harness.h"

#include "nn_kernels.h"
#include "inference.h"

#include <string.h>

TEST(rounding_divide_by_power_of_two_rounds_half_away_from_zero)
{
    CHECK_INT_EQ(cardia_rounding_div_pot(8, 1), 4);
    CHECK_INT_EQ(cardia_rounding_div_pot(9, 1), 5);   /* 4.5 -> 5 */
    CHECK_INT_EQ(cardia_rounding_div_pot(-9, 1), -5); /* -4.5 -> -5 */
    CHECK_INT_EQ(cardia_rounding_div_pot(7, 2), 2);   /* 1.75 -> 2 */
    CHECK_INT_EQ(cardia_rounding_div_pot(-7, 2), -2);
    CHECK_INT_EQ(cardia_rounding_div_pot(5, 0), 5);   /* no shift */
}

TEST(doubling_high_mul_matches_definition)
{
    /* (a*b) >> 31, rounded. The multiplier is read as Q31, so b = 2^30
     * represents 0.5 and the result is a/2. The "doubling" in the name is why
     * it is >> 31 and not >> 32: the product of two Q31 values is Q62, and
     * shifting by 31 rather than 32 renormalises it back to Q31 instead of
     * leaving it a factor of two small. */
    CHECK_INT_EQ(cardia_sat_doubling_high_mul(1 << 20, 1 << 30), (1 << 20) / 2);
    CHECK_INT_EQ(cardia_sat_doubling_high_mul(-(1 << 20), 1 << 30), -((1 << 20) / 2));
    /* b = 2^29 represents 0.25. */
    CHECK_INT_EQ(cardia_sat_doubling_high_mul(1 << 20, 1 << 29), (1 << 20) / 4);
    /* Identity multiplier: 2^31 - 1 is one LSB short of 1.0, so a large input
     * comes back essentially unchanged. */
    const int32_t v = 1000000;
    CHECK_NEAR(cardia_sat_doubling_high_mul(v, 0x7FFFFFFF), v, 1);
    /* The one case that must saturate rather than overflow. */
    CHECK_INT_EQ(cardia_sat_doubling_high_mul((int32_t)0x80000000, (int32_t)0x80000000),
                 (int32_t)0x7FFFFFFF);
}

TEST(requantize_scales_by_multiplier_and_shift)
{
    /* multiplier 2^30, shift 0  ->  x * 2^30 * 2^-31 = x/2 */
    CHECK_INT_EQ(cardia_requantize(1000, 1 << 30, 0), 500);
    /* shift -1 adds another halving */
    CHECK_INT_EQ(cardia_requantize(1000, 1 << 30, -1), 250);
    /* shift +1 doubles */
    CHECK_INT_EQ(cardia_requantize(1000, 1 << 30, 1), 1000);
    CHECK_INT_EQ(cardia_requantize(0, 1 << 30, -3), 0);
    CHECK_INT_EQ(cardia_requantize(-1000, 1 << 30, 0), -500);
}

TEST(saturation_clamps_to_int8)
{
    CHECK_INT_EQ(cardia_sat_int8(200), 127);
    CHECK_INT_EQ(cardia_sat_int8(-200), -128);
    CHECK_INT_EQ(cardia_sat_int8(42), 42);
}

TEST(quantize_f32_round_trips)
{
    const float in[4] = {0.0f, 1.0f, -1.0f, 0.5f};
    int8_t out[4];
    cardia_quantize_f32(in, 4, 0.01f, 0, out);
    CHECK_INT_EQ(out[0], 0);
    CHECK_INT_EQ(out[1], 100);
    CHECK_INT_EQ(out[2], -100);
    CHECK_INT_EQ(out[3], 50);
    /* Anything past the representable range must clamp, never wrap. A wrap
     * would turn a large positive artefact into a large negative sample. */
    const float big[2] = {100.0f, -100.0f};
    int8_t bout[2];
    cardia_quantize_f32(big, 2, 0.01f, 0, bout);
    CHECK_INT_EQ(bout[0], 127);
    CHECK_INT_EQ(bout[1], -128);
}

TEST(conv1d_identity_kernel_passes_signal_through)
{
    /* One input channel, one output channel, kernel {0,1,0}: the output should
     * be the input, requantised by 1.0. Multiplier 2^30 with shift +1 is
     * exactly 1.0. */
    const int8_t in[5] = {10, 20, 30, 40, 50};
    const int8_t w[3] = {0, 1, 0};
    const int32_t bias[1] = {0};
    const int32_t mult[1] = {1 << 30};
    const int32_t shift[1] = {1};
    int8_t out[5];
    cardia_conv1d_s8(in, 5, 1, w, bias, mult, shift,
                     1, 3, 1, 1, 0, 0, -128, 127, out, 5);
    for (int i = 0; i < 5; ++i) CHECK_INT_EQ(out[i], in[i]);
}

TEST(conv1d_zero_padding_uses_the_quantised_zero)
{
    /* With input_offset = 8 (i.e. zero-point -8), the quantised value that
     * represents real 0.0 is -8, not 0. A kernel that sums its taps must see
     * padding contribute nothing to the real-valued sum. This is the single
     * most common int8 convolution bug and it only shows up at the edges. */
    const int8_t in[4] = {-8, -8, -8, -8}; /* all real zeros */
    const int8_t w[3] = {1, 1, 1};
    const int32_t bias[1] = {0};
    const int32_t mult[1] = {1 << 30};
    const int32_t shift[1] = {1};
    int8_t out[4];
    cardia_conv1d_s8(in, 4, 1, w, bias, mult, shift,
                     1, 3, 1, 1, 8, 0, -128, 127, out, 4);
    /* Every real input is zero, so every output must be zero, including the
     * two edge positions that read padding. */
    for (int i = 0; i < 4; ++i) CHECK_INT_EQ(out[i], 0);
}

TEST(conv1d_applies_bias)
{
    const int8_t in[3] = {0, 0, 0};
    const int8_t w[1] = {1};
    const int32_t bias[1] = {40};
    const int32_t mult[1] = {1 << 30};
    const int32_t shift[1] = {1};
    int8_t out[3];
    cardia_conv1d_s8(in, 3, 1, w, bias, mult, shift,
                     1, 1, 1, 0, 0, 0, -128, 127, out, 3);
    for (int i = 0; i < 3; ++i) CHECK_INT_EQ(out[i], 40);
}

TEST(maxpool_takes_per_channel_maximum)
{
    /* Channels-last layout: {t0c0, t0c1, t1c0, t1c1, ...} */
    const int8_t in[8] = {1, 9, 5, 2, 7, 3, 0, 8};
    int8_t out[4];
    cardia_maxpool1d_s8(in, 4, 2, 2, out, 2);
    CHECK_INT_EQ(out[0], 5);  /* max(1,5) on channel 0 */
    CHECK_INT_EQ(out[1], 9);  /* max(9,2) on channel 1 */
    CHECK_INT_EQ(out[2], 7);  /* max(7,0) */
    CHECK_INT_EQ(out[3], 8);  /* max(3,8) */
}

TEST(global_avgpool_averages_over_time)
{
    /* Two time steps, one channel, values 20 and 40 with zero-point 0.
     * Sum = 60; a multiplier representing 1/2 gives 30. */
    const int8_t in[2] = {20, 40};
    int8_t out[1];
    cardia_global_avgpool_s8(in, 2, 1, 0, 0, 1 << 30, 0, -128, 127, out);
    CHECK_INT_EQ(out[0], 30);
}

TEST(fully_connected_s32_accumulates_exactly)
{
    const int8_t in[3] = {1, 2, 3};
    const int8_t w[6] = {1, 1, 1,    2, 0, -1};
    const int32_t b[2] = {10, -5};
    int32_t out[2];
    cardia_fully_connected_s32(in, 3, w, b, 2, 0, out);
    CHECK_INT_EQ(out[0], 1 + 2 + 3 + 10);
    CHECK_INT_EQ(out[1], 2 * 1 + 0 * 2 + (-1) * 3 - 5);
}

TEST(fully_connected_s32_applies_input_offset)
{
    const int8_t in[2] = {0, 0};
    const int8_t w[2] = {3, 4};
    const int32_t b[1] = {0};
    int32_t out[1];
    /* input_offset 5 means each stored 0 represents the real value 5 codes
     * above the zero-point. */
    cardia_fully_connected_s32(in, 2, w, b, 1, 5, out);
    CHECK_INT_EQ(out[0], 3 * 5 + 4 * 5);
}

TEST(classifier_is_deterministic_and_in_range)
{
    /* The exported model may or may not be present in a fresh clone; when it
     * is, the classifier must return a valid class and give the same answer
     * twice for the same input. Statelessness matters: the firmware calls this
     * from a loop and any leftover state would make results depend on history.
     */
    float beat[CARDIA_BEAT_LEN];
    float rr[CARDIA_N_RR_FEATURES] = {0.27f, 0.27f, 0.33f, 0.33f};
    for (int i = 0; i < CARDIA_BEAT_LEN; ++i) {
        beat[i] = __builtin_sinf((float)i * 0.05f);
    }
    int32_t l1[CARDIA_N_CLASSES], l2[CARDIA_N_CLASSES];
    const uint8_t a = cardia_classify(beat, rr, l1);
    const uint8_t b = cardia_classify(beat, rr, l2);
    CHECK(a < CARDIA_N_CLASSES);
    CHECK_INT_EQ(a, b);
    for (int i = 0; i < CARDIA_N_CLASSES; ++i) CHECK_INT_EQ(l1[i], l2[i]);
    printf("    scratch RAM: %u bytes\n", (unsigned)cardia_inference_scratch_bytes());
}

int main(void)
{
    printf("test_nn\n");
    RUN_TEST(rounding_divide_by_power_of_two_rounds_half_away_from_zero);
    RUN_TEST(doubling_high_mul_matches_definition);
    RUN_TEST(requantize_scales_by_multiplier_and_shift);
    RUN_TEST(saturation_clamps_to_int8);
    RUN_TEST(quantize_f32_round_trips);
    RUN_TEST(conv1d_identity_kernel_passes_signal_through);
    RUN_TEST(conv1d_zero_padding_uses_the_quantised_zero);
    RUN_TEST(conv1d_applies_bias);
    RUN_TEST(maxpool_takes_per_channel_maximum);
    RUN_TEST(global_avgpool_averages_over_time);
    RUN_TEST(fully_connected_s32_accumulates_exactly);
    RUN_TEST(fully_connected_s32_applies_input_offset);
    RUN_TEST(classifier_is_deterministic_and_in_range);
    return test_report("test_nn");
}
