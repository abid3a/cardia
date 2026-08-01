/* system_stm32f4xx.c -- bring the STM32F446RE to its maximum 180 MHz.
 *
 * ==========================================================================
 * RESULTING CLOCK TREE
 * ==========================================================================
 *
 *   HSE  8 MHz  (BYPASS: square wave from the ST-LINK's MCO, no crystal)
 *     |
 *     +-- /M = /4 ------------------> 2 MHz   PLL input
 *                                     (the datasheet wants 1-2 MHz here; 2 MHz
 *                                      is the top of that band and minimises
 *                                      PLL jitter for a given output)
 *     +-- xN = x180 ----------------> 360 MHz VCO
 *     +-- /P = /2 ------------------> 180 MHz SYSCLK
 *     +-- /Q = /7 ------------------> 51.4 MHz (USB OTG / SDIO; unused here,
 *                                      but Q must still be legal)
 *
 *   SYSCLK 180 MHz
 *     |
 *     +-- AHB  /1 ------------------> HCLK  180 MHz  (core, DMA, SRAM, GPIO)
 *     +-- APB1 /4 ------------------> PCLK1  45 MHz  (max 45 MHz -- TIM2, USART2, PWR)
 *     +-- APB2 /2 ------------------> PCLK2  90 MHz  (max 90 MHz -- ADC1)
 *
 * ==========================================================================
 * THE APB TIMER DOUBLING RULE, AND WHY CARDIA'S SAMPLE RATE DEPENDS ON IT
 * ==========================================================================
 *
 * RM0390 section 6.2: "If the APB prescaler is 1, the timer clock frequencies
 * are set to the same frequency as that of the APB domain. Otherwise, they are
 * set to twice (x2) the frequency of the APB domain."
 *
 * TIM2 is on APB1. The APB1 prescaler here is /4, which is not 1, so the timer
 * clock is doubled back up:
 *
 *     PCLK1     = 180 MHz / 4 = 45 MHz
 *     TIM2 clk  = 2 x 45 MHz  = 90 MHz     <- this is CARDIA_TIM_CLOCK_HZ
 *
 * The sample clock therefore divides 90 MHz, not 45 MHz:
 *
 *     f_sample = 90 MHz / ((PSC + 1) * (ARR + 1))
 *              = 90 000 000 / (50 * 5000)
 *              = 90 000 000 / 250 000
 *              = 360.000 Hz exactly
 *
 * Exactly, with no remainder -- which matters, because the model was trained on
 * MIT-BIH at 360 Hz and every filter coefficient in firmware/src/dsp was
 * designed at that rate. Assuming PCLK1 rather than the doubled timer clock
 * would give 180 Hz: half speed, every RR interval doubled, every beat
 * classified against filters designed for a different band. The firmware would
 * run happily and be wrong, which is the worst failure mode available.
 *
 * The 360 Hz figure is asserted at compile time in tim.c against
 * CARDIA_TIM_CLOCK_HZ / CARDIA_TIM_PSC / CARDIA_TIM_ARR from the generated
 * config header, so this reasoning is enforced rather than merely documented.
 *
 * ==========================================================================
 * WHY OVER-DRIVE
 * ==========================================================================
 *
 * The F446 runs to 168 MHz on the normal regulator path. Above that -- and 180
 * MHz is above that -- RM0390 section 5.1.3 requires the core supply be put in
 * over-drive: voltage scale 1, then ODEN, wait ODRDY, then ODSWEN, wait
 * ODSWRDY. Skipping it does not produce a clean failure; it produces a part
 * running outside its validated timing, which typically means intermittent
 * flash read errors under load. Bring-up passes, the bench passes, and the
 * device faults an hour in.
 */

#include <stdint.h>

#include "stm32f446_regs.h"
#include "system.h"

uint32_t SystemCoreClock = CARDIA_SYSCLK_HZ;

/* PLL dividers. Named so the arithmetic in the header comment is checkable
 * against the code without decoding shifted literals. */
#define PLL_M   4u     /* 8 MHz  / 4   = 2 MHz   VCO input   */
#define PLL_N   180u   /* 2 MHz  x 180 = 360 MHz VCO output  */
#define PLL_P   2u     /* 360MHz / 2   = 180 MHz SYSCLK      */
#define PLL_Q   7u     /* 360MHz / 7   = 51.4 MHz (OTG/SDIO) */

/* PLLP is encoded as (P/2 - 1): 0=/2, 1=/4, 2=/6, 3=/8. */
#define PLL_P_BITS  ((PLL_P / 2u) - 1u)

/* Flash wait states. RM0390 Table 5, VDD 2.7-3.6 V: 180 MHz needs 5 WS. */
#define FLASH_LATENCY_5WS  5u

/* Bounded spin so a dead oscillator produces a diagnosable stall instead of an
 * infinite loop that looks identical to a bricked board. Roughly 20 ms at any
 * clock this code can be running at when it is used. */
#define CLOCK_TIMEOUT  0x00100000u

static int wait_flag_set(volatile uint32_t *reg, uint32_t mask)
{
    uint32_t timeout = CLOCK_TIMEOUT;
    while ((*reg & mask) == 0u) {
        if (--timeout == 0u) {
            return 0;
        }
    }
    return 1;
}

/* If the clock tree cannot be established there is no safe way to continue:
 * every timing in the system, including the sample rate the model depends on,
 * would be wrong. Stop here so a debugger finds the machine at the exact point
 * of failure. */
static void clock_failure(void)
{
    for (;;) {
    }
}

void SystemInit(void)
{
    /* --- 0. Known state ---------------------------------------------------
     * Reset leaves HSI on and selected. Do not assume it: a warm reset from a
     * running system can arrive here with the PLL already on, and reconfiguring
     * a live PLL is ignored by the hardware. Fall back to HSI first. */
    RCC->CR |= RCC_CR_HSION;
    if (!wait_flag_set(&RCC->CR, RCC_CR_HSIRDY)) {
        clock_failure();
    }
    RCC->CFGR &= ~RCC_CFGR_SW_Msk;                 /* SW = HSI */
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != 0u) {
    }
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON);

    /* --- 1. HSE in bypass mode -------------------------------------------
     * BYPASS must be programmed while HSEON is clear; the bit is ignored
     * otherwise. On this board the 8 MHz comes from the ST-LINK MCU as a
     * digital clock, so the crystal driver must stay off -- with HSEBYP clear
     * the oscillator would try to drive a resonator that is not fitted and
     * HSERDY would never assert. */
    RCC->CR &= ~RCC_CR_HSEON;
    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    if (!wait_flag_set(&RCC->CR, RCC_CR_HSERDY)) {
        clock_failure();
    }

    /* --- 2. Voltage scale 1 -----------------------------------------------
     * PWR is on APB1 and its clock is off out of reset, so enabling it is a
     * prerequisite for touching PWR_CR at all -- writes to a clock-gated
     * peripheral are silently dropped. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;                            /* ensure the write landed */

    PWR->CR = (PWR->CR & ~PWR_CR_VOS_Msk) | PWR_CR_VOS_SCALE1;

    /* --- 3. Over-drive ----------------------------------------------------
     * Mandatory above 168 MHz. Order is fixed by RM0390 5.1.3: ODEN first and
     * wait for the regulator to reach the over-drive level, then ODSWEN to
     * actually switch the core supply onto it, and wait again. Running with
     * ODEN set but ODSWEN unset leaves the core still on the normal path. */
    PWR->CR |= PWR_CR_ODEN;
    if (!wait_flag_set(&PWR->CSR, PWR_CSR_ODRDY)) {
        clock_failure();
    }
    PWR->CR |= PWR_CR_ODSWEN;
    if (!wait_flag_set(&PWR->CSR, PWR_CSR_ODSWRDY)) {
        clock_failure();
    }

    /* --- 4. Flash timing --------------------------------------------------
     * Latency must be raised BEFORE the core speeds up, never after: the flash
     * cannot return data in time for a 180 MHz core at fewer wait states, and
     * the first instruction fetched too fast is the one that faults.
     *
     * Prefetch plus the instruction and data caches are what make 5 wait states
     * cost almost nothing in practice -- straight-line code and the model's
     * .rodata weights both stream out of the caches rather than stalling. On a
     * workload that reads several kilobytes of int8 weights per beat this is
     * not a micro-optimisation. */
    FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN
               | (FLASH_LATENCY_5WS << FLASH_ACR_LATENCY_Pos);
    while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk)
           != (FLASH_LATENCY_5WS << FLASH_ACR_LATENCY_Pos)) {
    }

    /* --- 5. Bus prescalers, set before the switch -------------------------
     * These must be in place while SYSCLK is still 16 MHz. If APB1 were left at
     * /1 when SYSCLK jumps to 180 MHz, PCLK1 would momentarily be 180 MHz
     * against a 45 MHz limit -- four times over spec, for however many cycles
     * it takes to get back here. */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk
                               | RCC_CFGR_PPRE2_Msk))
              | RCC_CFGR_HPRE_DIV1
              | RCC_CFGR_PPRE1_DIV4
              | RCC_CFGR_PPRE2_DIV2;

    /* --- 6. PLL ----------------------------------------------------------- */
    RCC->PLLCFGR = ((PLL_M   << RCC_PLLCFGR_PLLM_Pos) & RCC_PLLCFGR_PLLM_Msk)
                 | ((PLL_N   << RCC_PLLCFGR_PLLN_Pos) & RCC_PLLCFGR_PLLN_Msk)
                 | ((PLL_P_BITS << RCC_PLLCFGR_PLLP_Pos) & RCC_PLLCFGR_PLLP_Msk)
                 | ((PLL_Q   << RCC_PLLCFGR_PLLQ_Pos) & RCC_PLLCFGR_PLLQ_Msk)
                 | RCC_PLLCFGR_PLLSRC_HSE;

    RCC->CR |= RCC_CR_PLLON;
    if (!wait_flag_set(&RCC->CR, RCC_CR_PLLRDY)) {
        clock_failure();
    }

    /* --- 7. Switch SYSCLK to the PLL --------------------------------------
     * SW is the request; SWS is the acknowledgement. They are separate
     * registers' fields for a reason: the switch is not instantaneous, and
     * code that assumes it is will compute delays against the wrong frequency
     * for a few hundred cycles. Wait for SWS. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    {
        uint32_t timeout = CLOCK_TIMEOUT;
        while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) {
            if (--timeout == 0u) {
                clock_failure();
            }
        }
    }

    SystemCoreClock = CARDIA_SYSCLK_HZ;
}

void system_delay_us(uint32_t us)
{
    /* Four instructions per iteration on this core with -Os; the loop is a
     * lower bound on the delay, which is the safe direction for the settling
     * waits it is used for. `volatile` stops the optimiser deleting it. */
    volatile uint32_t iterations = (SystemCoreClock / 4000000u) * us;
    while (iterations-- != 0u) {
        __asm volatile ("nop");
    }
}
