#include "dwt.h"

#include "system.h"

void dwt_init(void)
{
    /* TRCENA gates the whole trace and debug block, DWT included. Without it
     * CYCCNT reads as a constant zero and every measurement silently reports
     * "0 cycles", which is a much worse failure than an error. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA;

    /* Stop, zero, start -- in that order, so the first interval measured after
     * init cannot straddle a reset of the counter. */
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA;
}

uint32_t dwt_cycles_to_us(uint32_t cycles)
{
    /* SystemCoreClock is 180e6, so cycles_per_us is 180 and the division is
     * exact. Guard against a zero clock anyway: this is the kind of helper that
     * gets called from a fault handler someday. */
    const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    return cycles_per_us ? (cycles / cycles_per_us) : 0u;
}

uint32_t dwt_cycles_to_ns(uint32_t cycles)
{
    const uint32_t mhz = SystemCoreClock / 1000000u;
    if (mhz == 0u) {
        return 0u;
    }
    /* cycles * 1000 overflows above ~4.29e6 cycles (23.9 ms at 180 MHz). Every
     * caller of this measures something far shorter; clamp rather than wrap so
     * a misuse is visible in the output instead of producing a plausible lie. */
    if (cycles > (0xFFFFFFFFu / 1000u)) {
        return 0xFFFFFFFFu;
    }
    return (cycles * 1000u) / mhz;
}
