/* adc_dma.h -- ADC1_IN0 sampled by TIM2 TRGO, moved by DMA2 Stream 0 into a
 * double buffer.
 *
 * ==========================================================================
 * WHY DOUBLE BUFFERING, AT LENGTH, BECAUSE THIS IS THE PART THAT BITES
 * ==========================================================================
 *
 * The DMA controller never stops. Once TIM2 is running it delivers a new
 * conversion result every 2.78 ms forever, whether or not the CPU is ready. The
 * only question a driver design answers is: where does the sample go while the
 * CPU is busy with the previous ones?
 *
 * With a SINGLE circular buffer of N samples, the DMA writes N, wraps, and
 * starts overwriting from the beginning. The CPU is notified at the wrap and
 * has exactly N sample periods to read all N samples before they are destroyed.
 * If it takes longer -- one long UART line, one inference that runs at the top
 * of its distribution, one interrupt at a slightly awkward moment -- the DMA
 * overwrites samples that have not been read. There is no flag for this. The
 * data simply changes underneath the reader, and the corruption is at the
 * *start* of the buffer, which is the oldest data, which is the part the CPU
 * has usually already consumed. So it half-works, most of the time.
 *
 * With DOUBLE BUFFERING (DMA_SxCR DBM, with M0AR and M1AR both populated), the
 * hardware alternates: it fills buffer A while the CPU reads buffer B, then
 * swaps at each transfer-complete. The CPU's deadline is unchanged -- it still
 * has N sample periods -- but the memory it is reading is genuinely not being
 * written. Missing the deadline now loses a whole buffer cleanly and, because
 * the CT bit says which buffer the DMA is currently filling, it is *detectable*
 * (see the overrun check in the ISR). A silent corruption becomes a counted
 * event.
 *
 * The specific cost of getting this wrong in Cardia, spelled out:
 *
 *   Pan-Tompkins tracks R peaks by absolute sample index. RR intervals are
 *   index differences. Two of the four features the classifier consumes are RR
 *   ratios, and they are precisely the features that separate a supraventricular
 *   ectopic (class S) from a normal beat -- an S beat looks normal and is
 *   distinguished almost entirely by arriving early. Drop a block of samples at
 *   360 Hz and every subsequent index is shifted; one RR interval is wrong by
 *   the size of the gap, and the two beats on either side of it get RR features
 *   from a rhythm that did not happen. The classifier then confidently reports
 *   an arrhythmia that is an artefact of the buffer, or misses a real one. It
 *   does not crash. It does not log anything. It is just wrong, occasionally,
 *   in a way that looks like a model accuracy problem rather than a driver bug.
 *
 * That asymmetry -- cheap to prevent, extremely expensive to debug -- is the
 * entire argument for spending a second buffer of 64 bytes on it.
 *
 * ==========================================================================
 * SIGNAL PATH
 * ==========================================================================
 *
 *   PA0 (analog) -> ADC1_IN0, 12-bit, triggered by TIM2_TRGO rising edge
 *                -> ADC1->DR
 *                -> DMA2 Stream 0, Channel 0 (the ADC1 request line)
 *                -> buf0 / buf1, alternating, halfword transfers
 *                -> DMA2_Stream0_IRQHandler -> adc_dma_on_buffer()
 *
 * No CPU involvement between the timer edge and the buffer being full.
 */

#ifndef CARDIA_ADC_DMA_H
#define CARDIA_ADC_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure PA0, ADC1 and DMA2 Stream 0. `buf0` and `buf1` must each hold
 * `len` uint16_t and must live for the lifetime of the program -- they are
 * written by hardware, so they cannot be stack objects. Does not start
 * conversion; call adc_dma_start() and then tim2_start(). */
void adc_dma_init(uint16_t *buf0, uint16_t *buf1, uint32_t len);

/* Enable the stream and the ADC. Conversions begin on the next TIM2 update. */
void adc_dma_start(void);

void adc_dma_stop(void);

/* Called from the DMA transfer-complete interrupt with the buffer that has just
 * been filled -- which is guaranteed not to be the one the DMA is now writing.
 *
 * Declared weak with an empty default so the driver links and runs standalone
 * during bring-up. The application overrides it. Keep the override short: it
 * executes in interrupt context, and anything slow enough to overrun the next
 * buffer will be counted by adc_dma_overruns() but the samples are still gone.
 */
void adc_dma_on_buffer(const uint16_t *buf, uint32_t len);

/* --- instrumentation -----------------------------------------------------
 * Counters, not flags: a system that drops one buffer an hour and a system that
 * drops one a second both "have overruns", and only the number distinguishes
 * them. Reported over UART so the health of the acquisition path is visible in
 * the same log as the classifications.
 */

/* Buffers the callback failed to finish before the DMA swapped again. Every
 * increment is CARDIA_ADC_BLOCK samples that never reached the pipeline. */
uint32_t adc_dma_overruns(void);

/* DMA transfer/FIFO/direct-mode errors. Non-zero means a hardware-level fault
 * in the transfer itself, which is a different problem from being too slow. */
uint32_t adc_dma_errors(void);

/* Half-transfer ticks. HTIE is enabled so the driver gets a mid-buffer event:
 * it is the halfway mark of the processing budget for the buffer in flight, and
 * a monotonically advancing count is the cheapest proof the acquisition chain
 * is still alive even when no beats are being detected. */
uint32_t adc_dma_half_ticks(void);

/* Buffers successfully delivered to the callback. */
uint32_t adc_dma_blocks(void);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_ADC_DMA_H */
