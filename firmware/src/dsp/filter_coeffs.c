/* filter_coeffs.c -- fixed IIR coefficients for the Cardia signal chain.
 *
 * GENERATED FILE. Do not edit by hand.
 * Regenerate: python ml/scripts/gen_filter_coeffs.py
 *
 * Both filters are Butterworth, designed with scipy.signal.butter(...,
 * output='sos') at fs = 360 Hz, and stored as scipy `sos` rows with a0
 * divided out:
 *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 */

#include "filters.h"

/* 0.5-40.0 Hz conditioning band -- feeds the classifier, preserves P/QRS/T morphology */
const cardia_biquad_coeffs_t cardia_bandpass_sos[] = {
    { +7.876235329e-02f, +1.575247066e-01f, +7.876235329e-02f, -1.067092429e+00f, +3.842342474e-01f },
    { +1.000000000e+00f, -2.000000000e+00f, +1.000000000e+00f, -1.987664221e+00f, +9.877417210e-01f },
};

/* 5.0-15.0 Hz detection band -- feeds Pan-Tompkins, deliberately suppresses P and T */
const cardia_biquad_coeffs_t cardia_qrs_bandpass_sos[] = {
    { +6.765413257e-03f, +1.353082651e-02f, +6.765413257e-03f, -1.791933833e+00f, +8.412220576e-01f },
    { +1.000000000e+00f, -2.000000000e+00f, +1.000000000e+00f, -1.919372617e+00f, +9.287446452e-01f },
};

