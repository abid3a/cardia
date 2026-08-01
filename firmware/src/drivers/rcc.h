/* rcc.h -- peripheral clock gating.
 *
 * A peripheral whose clock is off does not fault when written; it silently
 * discards the write and reads back zero. That failure mode has cost more
 * bring-up hours across the industry than almost anything else, so every
 * driver in this tree calls its rcc_enable_* helper as the first line of its
 * init function rather than relying on a central "turn everything on" routine
 * that can drift out of sync with what is actually used.
 */

#ifndef CARDIA_RCC_H
#define CARDIA_RCC_H

#ifdef __cplusplus
extern "C" {
#endif

void rcc_enable_gpioa(void);
void rcc_enable_gpiob(void);
void rcc_enable_gpioc(void);
void rcc_enable_dma2(void);
void rcc_enable_adc1(void);
void rcc_enable_tim2(void);
void rcc_enable_usart2(void);
void rcc_enable_pwr(void);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_RCC_H */
