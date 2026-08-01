#include "inference.h"
#include "nn_kernels.h"
#include "cardia_model.h"

#ifdef CARDIA_USE_CMSIS_NN
#include "arm_nnfunctions.h"
#endif

const char *const cardia_class_names[CARDIA_N_CLASSES] = {
    "N", "S", "V", "F", "Q"
};

/* --- Activation scratch ---------------------------------------------------
 * Two ping-pong buffers sized to the largest intermediate tensor (64x8 = 512
 * bytes for conv1's output, equal to conv2's and conv3's). Every layer reads
 * one and writes the other, so peak activation RAM is 1 KB regardless of depth
 * -- a tensor is never alive after its consumer has run.
 *
 * Static, file-scope, never malloc'd. On a device that must not fail at 3 a.m.
 * the allocation either succeeds at link time or the build does not fit; there
 * is no third outcome and no heap to fragment. This also makes the RAM figure
 * in docs/RESULTS.md exact rather than probabilistic.
 */
#define SCRATCH_BYTES 512

static int8_t s_buf_a[SCRATCH_BYTES];
static int8_t s_buf_b[SCRATCH_BYTES];
static int8_t s_fused[CARDIA_C3_OUT + CARDIA_N_RR_FEATURES];
static int8_t s_fc1_out[CARDIA_FC1_OUT];
static int32_t s_logits[CARDIA_N_CLASSES];

uint32_t cardia_inference_scratch_bytes(void)
{
    return (uint32_t)(sizeof(s_buf_a) + sizeof(s_buf_b) + sizeof(s_fused)
                      + sizeof(s_fc1_out) + sizeof(s_logits));
}

uint8_t cardia_classify(const float *beat, const float *rr, int32_t *logits)
{
    /* --- quantise the input window ------------------------------------- */
    cardia_quantize_f32(beat, CARDIA_BEAT_LEN, CARDIA_INPUT_SCALE,
                        CARDIA_INPUT_ZP, s_buf_a);

    /* --- conv1 + ReLU + maxpool ------------------------------------------
     * The ReLU is not a separate pass. Clamping the requantised result at the
     * output zero-point -- the int8 code that represents real 0.0 -- IS a
     * ReLU. Folding it into the clamp the kernel already performs saves a full
     * traversal of every activation tensor. */
    cardia_conv1d_s8(s_buf_a, CARDIA_BEAT_LEN, 1,
                     cardia_conv1_w, cardia_conv1_b,
                     cardia_conv1_mult, cardia_conv1_shift,
                     CARDIA_C1_OUT, CARDIA_C1_K, CARDIA_C1_S, CARDIA_C1_P,
                     -CARDIA_INPUT_ZP, CARDIA_C1_OUT_ZP,
                     CARDIA_C1_OUT_ZP, 127,
                     s_buf_b, CARDIA_L1);
    cardia_maxpool1d_s8(s_buf_b, CARDIA_L1, CARDIA_C1_OUT, 2, s_buf_a, CARDIA_L1P);

    /* --- conv2 + ReLU + maxpool ------------------------------------------ */
    cardia_conv1d_s8(s_buf_a, CARDIA_L1P, CARDIA_C1_OUT,
                     cardia_conv2_w, cardia_conv2_b,
                     cardia_conv2_mult, cardia_conv2_shift,
                     CARDIA_C2_OUT, CARDIA_C2_K, CARDIA_C2_S, CARDIA_C2_P,
                     -CARDIA_C1_OUT_ZP, CARDIA_C2_OUT_ZP,
                     CARDIA_C2_OUT_ZP, 127,
                     s_buf_b, CARDIA_L2);
    cardia_maxpool1d_s8(s_buf_b, CARDIA_L2, CARDIA_C2_OUT, 2, s_buf_a, CARDIA_L2P);

    /* --- conv3 + ReLU ---------------------------------------------------- */
    cardia_conv1d_s8(s_buf_a, CARDIA_L2P, CARDIA_C2_OUT,
                     cardia_conv3_w, cardia_conv3_b,
                     cardia_conv3_mult, cardia_conv3_shift,
                     CARDIA_C3_OUT, CARDIA_C3_K, CARDIA_C3_S, CARDIA_C3_P,
                     -CARDIA_C2_OUT_ZP, CARDIA_C3_OUT_ZP,
                     CARDIA_C3_OUT_ZP, 127,
                     s_buf_b, CARDIA_L3);

    /* --- global average pool over time ----------------------------------
     * Collapses the 16 remaining time steps to one value per channel. Chosen
     * over a flatten-then-dense head for two reasons: it removes 16x the
     * parameters from the classifier head, and it makes the network invariant
     * to small errors in where the R peak was located -- which matters,
     * because Pan-Tompkins does not place the fiducial point perfectly. */
    cardia_global_avgpool_s8(s_buf_b, CARDIA_L3, CARDIA_C3_OUT,
                             -CARDIA_C3_OUT_ZP, CARDIA_FUSED_ZP,
                             CARDIA_GAP_MULT, CARDIA_GAP_SHIFT,
                             -128, 127, s_fused);

    /* --- fuse the RR features -------------------------------------------
     * Quantised with the SAME scale and zero-point as the pooled morphology
     * features, because the two are concatenated into one int8 vector and a
     * single tensor cannot carry two scales. */
    cardia_quantize_f32(rr, CARDIA_N_RR_FEATURES, CARDIA_FUSED_SCALE,
                        CARDIA_FUSED_ZP, s_fused + CARDIA_C3_OUT);

    /* --- fc1 + ReLU ------------------------------------------------------ */
    cardia_fully_connected_s8(s_fused, CARDIA_FUSED_LEN,
                              cardia_fc1_w, cardia_fc1_b, CARDIA_FC1_OUT,
                              -CARDIA_FUSED_ZP, CARDIA_FC1_OUT_ZP,
                              CARDIA_FC1_MULT, CARDIA_FC1_SHIFT,
                              CARDIA_FC1_OUT_ZP, 127, s_fc1_out);

    /* --- fc2, kept as int32 ---------------------------------------------
     * The final layer's accumulators are NOT requantised to int8. Requantising
     * would quantise away the very margin between the top two classes, which
     * is the only thing argmax depends on. Keeping int32 costs 20 bytes and
     * makes the decision exact. It is also why fc2's weights use a per-tensor
     * scale: with per-channel scales the five accumulators would be expressed
     * in five different units and comparing them would be meaningless. */
    cardia_fully_connected_s32(s_fc1_out, CARDIA_FC1_OUT,
                               cardia_fc2_w, cardia_fc2_b, CARDIA_N_CLASSES,
                               -CARDIA_FC1_OUT_ZP, s_logits);

    uint8_t best = 0;
    for (uint8_t i = 1; i < CARDIA_N_CLASSES; ++i) {
        if (s_logits[i] > s_logits[best]) best = i;
    }
    if (logits) {
        for (uint8_t i = 0; i < CARDIA_N_CLASSES; ++i) logits[i] = s_logits[i];
    }
    return best;
}
