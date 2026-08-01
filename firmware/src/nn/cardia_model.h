/* cardia_model.h -- quantised model parameters.
 *
 * GENERATED FILE. Do not edit by hand.
 * Regenerate: python ml/scripts/export_model.py
 *
 * Layout notes:
 *   conv weights  (out_ch, kernel, in_ch)  -- channels last, as CMSIS-NN wants
 *   fc weights    (out, in)                -- row major
 *   biases        int32, pre-divided into accumulator units (S_in * S_w)
 *   mult/shift    per output channel for convolutions, per tensor for fc
 */

#ifndef CARDIA_MODEL_H
#define CARDIA_MODEL_H

#include <stdint.h>

/* --- geometry --- */
#define CARDIA_C1_OUT   8
#define CARDIA_C1_K     11
#define CARDIA_C1_S     4
#define CARDIA_C1_P     5
#define CARDIA_C2_OUT   16
#define CARDIA_C2_K     7
#define CARDIA_C2_S     1
#define CARDIA_C2_P     3
#define CARDIA_C3_OUT   32
#define CARDIA_C3_K     5
#define CARDIA_C3_S     1
#define CARDIA_C3_P     2
#define CARDIA_L1       64
#define CARDIA_L1P      32
#define CARDIA_L2       32
#define CARDIA_L2P      16
#define CARDIA_L3       16
#define CARDIA_FC1_OUT  32
#define CARDIA_FUSED_LEN 36

/* --- quantisation parameters --- */
#define CARDIA_INPUT_SCALE  4.318627937e-02f
#define CARDIA_INPUT_ZP     (-22)
#define CARDIA_C1_OUT_ZP    (-128)
#define CARDIA_C2_OUT_ZP    (-128)
#define CARDIA_C3_OUT_ZP    (-128)
#define CARDIA_FUSED_SCALE  6.670385716e-03f
#define CARDIA_FUSED_ZP     (-128)
#define CARDIA_GAP_MULT     (2110708530)
#define CARDIA_GAP_SHIFT    (-2)
#define CARDIA_FC1_MULT     (1618827887)
#define CARDIA_FC1_SHIFT    (-8)
#define CARDIA_FC1_OUT_ZP   (-128)

/* Real-world value of one logit LSB. Only needed to print a confidence; the
 * argmax decision does not use it. */
#define CARDIA_LOGIT_SCALE  3.577456286e-04f

/* --- parameters --- */
extern const int8_t  cardia_conv1_w[88];
extern const int32_t cardia_conv1_b[8];
extern const int32_t cardia_conv1_mult[8];
extern const int32_t cardia_conv1_shift[8];
extern const int8_t  cardia_conv2_w[896];
extern const int32_t cardia_conv2_b[16];
extern const int32_t cardia_conv2_mult[16];
extern const int32_t cardia_conv2_shift[16];
extern const int8_t  cardia_conv3_w[2560];
extern const int32_t cardia_conv3_b[32];
extern const int32_t cardia_conv3_mult[32];
extern const int32_t cardia_conv3_shift[32];
extern const int8_t  cardia_fc1_w[1152];
extern const int32_t cardia_fc1_b[32];
extern const int8_t  cardia_fc2_w[160];
extern const int32_t cardia_fc2_b[5];

/* Bytes of model parameters in .rodata. */
#define CARDIA_MODEL_PARAM_BYTES 5676

#endif /* CARDIA_MODEL_H */
