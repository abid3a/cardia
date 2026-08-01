/* dwt.h -- cycle-accurate timing from the Cortex-M4 debug unit.
 *
 * Why the DWT cycle counter and not a hardware timer
 * --------------------------------------------------
 * Every latency number this project publishes -- "inference takes N
 * microseconds" -- has to come from somewhere trustworthy, and the options are
 * a GPIO toggled against a scope, a spare timer, or DWT_CYCCNT. CYCCNT wins on
 * all three axes that matter:
 *
 *   * Resolution. It counts CPU cycles, so at 180 MHz one tick is 5.6 ns. A
 *     timer clocked off APB1 gives 11 ns at best and only after burning a
 *     prescaler on it.
 *   * Cost. Reading it is a single load from the private peripheral bus. There
 *     is no start, no capture register, no interrupt, and no arithmetic to
 *     reconcile prescalers -- so the measurement does not meaningfully perturb
 *     what is being measured, which is the whole problem with instrumenting a
 *     50 microsecond routine.
 *   * Availability. It is free-running and 32-bit. Cardia has exactly two
 *     general-purpose timers it could spare, and TIM2 is already the sample
 *     clock; spending another on instrumentation would be spending a peripheral
 *     to measure a peripheral.
 *
 * The counter wraps every 2^32 cycles = 23.9 s at 180 MHz. Unsigned subtraction
 * makes the wrap transparent for any interval shorter than that, which every
 * interval measured here is by four orders of magnitude.
 *
 * One caveat, stated because it will eventually confuse someone: CYCCNT lives
 * in the debug block. TRCENA in DEMCR must be set for it to run at all, which
 * dwt_init() does. It does not require a debugger to be attached.
 */

#ifndef CARDIA_DWT_H
#define CARDIA_DWT_H

#include <stdint.h>

#include "stm32f446_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enable TRCENA, zero the counter, start it. Idempotent. */
void dwt_init(void);

/* Raw counter. Free-running from dwt_init() onwards. */
static inline uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

/* Interval helpers. Deliberately just aliases for dwt_cycles(): naming the two
 * ends of a measurement reads better at the call site than two identical
 * reads, and there is no state to keep.
 *
 *     uint32_t t0 = dwt_start();
 *     ... work ...
 *     uint32_t cyc = dwt_stop() - t0;
 */
static inline uint32_t dwt_start(void) { return DWT->CYCCNT; }
static inline uint32_t dwt_stop(void)  { return DWT->CYCCNT; }

/* Elapsed cycles between two reads, wrap-safe by unsigned arithmetic. */
static inline uint32_t dwt_elapsed(uint32_t start, uint32_t stop)
{
    return stop - start;
}

/* Convert a cycle count to microseconds using the live SystemCoreClock, so the
 * conversion cannot silently drift if the clock configuration ever changes.
 * Integer maths throughout -- this is called from paths that must not pull the
 * FPU into a measurement of themselves. */
uint32_t dwt_cycles_to_us(uint32_t cycles);

/* Nanoseconds, for intervals short enough that microseconds round to zero. */
uint32_t dwt_cycles_to_ns(uint32_t cycles);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_DWT_H */
