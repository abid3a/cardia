#include "tim.h"

#include "cardia_config.h"
#include "rcc.h"
#include "stm32f446_regs.h"
#include "system.h"

/* TIM2 is on APB1. The APB1 prescaler is /4 -- not 1 -- so per RM0390 6.2 the
 * timer clock is twice PCLK1: 2 x 45 MHz = 90 MHz, which is what
 * CARDIA_TIM_CLOCK_HZ says. Assuming PCLK1 instead would halve the sample rate
 * to 180 Hz and quietly invalidate every filter coefficient and RR interval in
 * the system.
 *
 * These are compile-time assertions rather than comments so the reasoning is
 * enforced by the build. If anyone retunes the clock tree, the firmware stops
 * compiling instead of starting to sample at the wrong rate. */
_Static_assert(CARDIA_TIM_CLOCK_HZ == 2u * CARDIA_PCLK1_HZ,
               "TIM2 clock must be twice PCLK1 (APB1 prescaler is not 1)");
_Static_assert(CARDIA_TIM_CLOCK_HZ
                   == (uint32_t)CARDIA_FS_HZ * (CARDIA_TIM_PSC + 1u)
                          * (CARDIA_TIM_ARR + 1u),
               "PSC/ARR must divide the timer clock to exactly CARDIA_FS_HZ");

void tim2_trigger_init(void)
{
    rcc_enable_tim2();

    tim2_stop();

    TIM2->PSC = CARDIA_TIM_PSC;    /* 49  -> 90 MHz / 50   = 1.8 MHz  */
    TIM2->ARR = CARDIA_TIM_ARR;    /* 4999 -> 1.8 MHz / 5000 = 360 Hz */
    TIM2->CNT = 0u;

    /* MMS = 010: the update event is the trigger output. */
    TIM2->CR2 = (TIM2->CR2 & ~TIM_CR2_MMS_Msk) | TIM_CR2_MMS_UPDATE;

    /* ARPE buffers ARR through a shadow register. Not strictly needed while ARR
     * is constant, but it means a future rate change cannot take effect
     * mid-period and emit one short interval. */
    TIM2->CR1 = TIM_CR1_ARPE;

    /* No DIER bits: this timer never interrupts the CPU. */
    TIM2->DIER = 0u;

    /* Force one update so PSC and ARR are loaded from their preload registers
     * before the counter runs -- otherwise the very first period uses the
     * reset values. Clear the flag it raises so nothing later mistakes it for
     * a real overflow. */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR &= ~TIM_SR_UIF;
}

void tim2_start(void)
{
    TIM2->CNT = 0u;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void tim2_stop(void)
{
    TIM2->CR1 &= ~TIM_CR1_CEN;
}
