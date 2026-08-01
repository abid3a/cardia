"""Emit the quantised model as C source.

The alternative -- shipping a .tflite file and linking a runtime interpreter --
was rejected deliberately. An interpreter costs tens of kilobytes of flash to
walk a graph that is fixed at build time and will never change on the device,
and it moves the weights into RAM. Generating straight-line C puts every weight
in .rodata (executed in place from flash, costing zero RAM), lets the linker
garbage-collect anything unused, and makes the whole network readable in a
diff. For a five-layer network that is strictly better; an interpreter earns
its keep only when the model can change without a rebuild.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import torch

from . import config as cfg, model as M
from .quantize import QuantModel

HEADER_NAME = "cardia_model.h"
SOURCE_NAME = "cardia_model.c"


def _c_array(name: str, values, ctype: str, per_line: int = 12) -> str:
    vals = [str(int(v)) for v in np.asarray(values).reshape(-1)]
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append("    " + ", ".join(vals[i:i + per_line]) + ",")
    body = "\n".join(lines)
    return f"const {ctype} {name}[{len(vals)}] = {{\n{body}\n}};\n\n"


def _fmt_f(v: float) -> str:
    return f"{v:.9e}f"


def generate(qm: QuantModel, out_dir: Path) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    L = qm.layers

    # torch conv weights are (out_ch, in_ch, K); the C kernels walk channels
    # last, so the tap and channel axes are swapped here rather than in the
    # inner loop on the MCU.
    w1 = L["conv1"].weight_q.numpy().transpose(0, 2, 1)
    w2 = L["conv2"].weight_q.numpy().transpose(0, 2, 1)
    w3 = L["conv3"].weight_q.numpy().transpose(0, 2, 1)
    wrr = L["rr_fc"].weight_q.numpy()
    wfc1 = L["fc1"].weight_q.numpy()
    wfc2 = L["fc2"].weight_q.numpy()

    header = f"""/* {HEADER_NAME} -- quantised model parameters.
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
#define CARDIA_C1_OUT   {M.C1_OUT}
#define CARDIA_C1_K     {M.C1_K}
#define CARDIA_C1_S     {M.C1_S}
#define CARDIA_C1_P     {M.C1_P}
#define CARDIA_C2_OUT   {M.C2_OUT}
#define CARDIA_C2_K     {M.C2_K}
#define CARDIA_C2_S     {M.C2_S}
#define CARDIA_C2_P     {M.C2_P}
#define CARDIA_C3_OUT   {M.C3_OUT}
#define CARDIA_C3_K     {M.C3_K}
#define CARDIA_C3_S     {M.C3_S}
#define CARDIA_C3_P     {M.C3_P}
#define CARDIA_L1       {M.L1}
#define CARDIA_L1P      {M.L1P}
#define CARDIA_L2       {M.L2}
#define CARDIA_L2P      {M.L2P}
#define CARDIA_L3       {M.L3}
#define CARDIA_RR_HIDDEN {M.RR_HIDDEN}
#define CARDIA_FC1_OUT  {M.FC1_OUT}
#define CARDIA_FUSED_LEN {M.FUSED_IN}

/* --- quantisation parameters --- */
#define CARDIA_INPUT_SCALE  {_fmt_f(qm.input_scale)}
#define CARDIA_INPUT_ZP     ({qm.input_zp})
#define CARDIA_C1_OUT_ZP    ({L['conv1'].output_zp})
#define CARDIA_C2_OUT_ZP    ({L['conv2'].output_zp})
#define CARDIA_C3_OUT_ZP    ({L['conv3'].output_zp})
#define CARDIA_RR_IN_SCALE  {_fmt_f(qm.rr_scale)}
#define CARDIA_RR_IN_ZP     ({qm.rr_zp})
#define CARDIA_RRFC_MULT    ({L['rr_fc'].mult[0]})
#define CARDIA_RRFC_SHIFT   ({L['rr_fc'].shift[0]})
#define CARDIA_FUSED_SCALE  {_fmt_f(qm.fused_scale)}
#define CARDIA_FUSED_ZP     ({qm.fused_zp})
#define CARDIA_GAP_MULT     ({L['gap'].mult[0]})
#define CARDIA_GAP_SHIFT    ({L['gap'].shift[0]})
#define CARDIA_FC1_MULT     ({L['fc1'].mult[0]})
#define CARDIA_FC1_SHIFT    ({L['fc1'].shift[0]})
#define CARDIA_FC1_OUT_ZP   ({L['fc1'].output_zp})

/* Real-world value of one logit LSB. Only needed to print a confidence; the
 * argmax decision does not use it. */
#define CARDIA_LOGIT_SCALE  {_fmt_f(qm.logit_scale)}

/* --- parameters --- */
extern const int8_t  cardia_conv1_w[{w1.size}];
extern const int32_t cardia_conv1_b[{L['conv1'].bias_q.numel()}];
extern const int32_t cardia_conv1_mult[{len(L['conv1'].mult)}];
extern const int32_t cardia_conv1_shift[{len(L['conv1'].shift)}];
extern const int8_t  cardia_conv2_w[{w2.size}];
extern const int32_t cardia_conv2_b[{L['conv2'].bias_q.numel()}];
extern const int32_t cardia_conv2_mult[{len(L['conv2'].mult)}];
extern const int32_t cardia_conv2_shift[{len(L['conv2'].shift)}];
extern const int8_t  cardia_conv3_w[{w3.size}];
extern const int32_t cardia_conv3_b[{L['conv3'].bias_q.numel()}];
extern const int32_t cardia_conv3_mult[{len(L['conv3'].mult)}];
extern const int32_t cardia_conv3_shift[{len(L['conv3'].shift)}];
extern const int8_t  cardia_rrfc_w[{wrr.size}];
extern const int32_t cardia_rrfc_b[{L['rr_fc'].bias_q.numel()}];
extern const int8_t  cardia_fc1_w[{wfc1.size}];
extern const int32_t cardia_fc1_b[{L['fc1'].bias_q.numel()}];
extern const int8_t  cardia_fc2_w[{wfc2.size}];
extern const int32_t cardia_fc2_b[{L['fc2'].bias_q.numel()}];

/* Bytes of model parameters in .rodata. */
#define CARDIA_MODEL_PARAM_BYTES {(w1.size + w2.size + w3.size + wrr.size + wfc1.size + wfc2.size) + 4 * (L['conv1'].bias_q.numel() + L['conv2'].bias_q.numel() + L['conv3'].bias_q.numel() + L['rr_fc'].bias_q.numel() + L['fc1'].bias_q.numel() + L['fc2'].bias_q.numel() + len(L['conv1'].mult) * 2 + len(L['conv2'].mult) * 2 + len(L['conv3'].mult) * 2)}

#endif /* CARDIA_MODEL_H */
"""

    src = f'/* {SOURCE_NAME} -- GENERATED FILE, do not edit. */\n\n#include "{HEADER_NAME}"\n\n'
    src += _c_array("cardia_conv1_w", w1, "int8_t")
    src += _c_array("cardia_conv1_b", L["conv1"].bias_q.numpy(), "int32_t", 8)
    src += _c_array("cardia_conv1_mult", L["conv1"].mult, "int32_t", 6)
    src += _c_array("cardia_conv1_shift", L["conv1"].shift, "int32_t", 12)
    src += _c_array("cardia_conv2_w", w2, "int8_t")
    src += _c_array("cardia_conv2_b", L["conv2"].bias_q.numpy(), "int32_t", 8)
    src += _c_array("cardia_conv2_mult", L["conv2"].mult, "int32_t", 6)
    src += _c_array("cardia_conv2_shift", L["conv2"].shift, "int32_t", 12)
    src += _c_array("cardia_conv3_w", w3, "int8_t")
    src += _c_array("cardia_conv3_b", L["conv3"].bias_q.numpy(), "int32_t", 8)
    src += _c_array("cardia_conv3_mult", L["conv3"].mult, "int32_t", 6)
    src += _c_array("cardia_conv3_shift", L["conv3"].shift, "int32_t", 12)
    src += _c_array("cardia_rrfc_w", wrr, "int8_t")
    src += _c_array("cardia_rrfc_b", L["rr_fc"].bias_q.numpy(), "int32_t", 8)
    src += _c_array("cardia_fc1_w", wfc1, "int8_t")
    src += _c_array("cardia_fc1_b", L["fc1"].bias_q.numpy(), "int32_t", 8)
    src += _c_array("cardia_fc2_w", wfc2, "int8_t")
    src += _c_array("cardia_fc2_b", L["fc2"].bias_q.numpy(), "int32_t", 8)

    (out_dir / HEADER_NAME).write_text(header)
    (out_dir / SOURCE_NAME).write_text(src)

    param_bytes = int(w1.size + w2.size + w3.size + wrr.size + wfc1.size + wfc2.size)
    aux_bytes = 4 * int(
        L["conv1"].bias_q.numel() + L["conv2"].bias_q.numel() + L["conv3"].bias_q.numel()
        + L["rr_fc"].bias_q.numel() + L["fc1"].bias_q.numel() + L["fc2"].bias_q.numel()
        + 2 * (len(L["conv1"].mult) + len(L["conv2"].mult) + len(L["conv3"].mult))
    )
    return {
        "weight_bytes_int8": param_bytes,
        "bias_and_requant_bytes": aux_bytes,
        "total_rodata_bytes": param_bytes + aux_bytes,
        "header": str(out_dir / HEADER_NAME),
        "source": str(out_dir / SOURCE_NAME),
    }
