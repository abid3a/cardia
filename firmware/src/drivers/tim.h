/* tim.h -- TIM2 as the 360 Hz sample clock.
 *
 * TIM2 does not raise an interrupt and the CPU never reads it. Its only job is
 * to emit a TRGO pulse on every update event, which is wired internally to
 * ADC1's external trigger input. Sampling therefore happens entirely in
 * hardware: timer -> ADC -> DMA, with no software in the loop.
 *
 * That matters more than it might look. If the CPU had to service a 360 Hz
 * interrupt and start each conversion, every sample's timestamp would carry the
 * jitter of whatever else was executing -- and this pipeline turns sample
 * indices into RR intervals, which are then features the classifier depends on.
 * Hardware triggering makes the sample interval exact regardless of software
 * load.
 */

#ifndef CARDIA_TIM_H
#define CARDIA_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Program PSC/ARR from the generated config header and select the update event
 * as TRGO. Does not start the counter. */
void tim2_trigger_init(void);

void tim2_start(void);
void tim2_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_TIM_H */
