/* inference.h -- int8 CNN beat classifier.
 *
 * Portable C. The host simulator and the MCU call this identical function on
 * identical inputs and must produce identical outputs, which is what makes
 * "the firmware runs the model that was evaluated" a checkable statement
 * rather than a hope.
 */

#ifndef CARDIA_INFERENCE_H
#define CARDIA_INFERENCE_H

#include <stdint.h>

#include "../dsp/cardia_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Classify one beat.
 *
 *   beat    CARDIA_BEAT_LEN samples, already per-beat z-scored
 *   rr      CARDIA_N_RR_FEATURES timing features
 *   logits  optional; receives the final layer's raw int32 accumulators
 *
 * Returns the AAMI class index: 0=N 1=S 2=V 3=F 4=Q.
 */
uint8_t cardia_classify(const float *beat, const float *rr, int32_t *logits);

/* Total bytes of static scratch the classifier owns. Reported in the resource
 * table so the RAM budget is a measured number, not an estimate. */
uint32_t cardia_inference_scratch_bytes(void);

/* Human-readable class names, indexed by the return value above. */
extern const char *const cardia_class_names[CARDIA_N_CLASSES];

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_INFERENCE_H */
