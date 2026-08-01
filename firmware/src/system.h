/* system.h -- clock-tree entry point and the derived frequencies.
 *
 * Every driver that computes a divider (USART baud, ADC prescaler, TIM
 * prescaler) takes its bus frequency from the macros here rather than from a
 * local literal, so there is exactly one place where "what is APB1 running at"
 * is answered.
 */

#ifndef CARDIA_SYSTEM_H
#define CARDIA_SYSTEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board oscillator: the Nucleo-F446RE has no crystal fitted. The ST-LINK MCU
 * drives an 8 MHz square wave into PH0, which is why HSE runs in BYPASS mode
 * rather than as a crystal oscillator. */
#define CARDIA_HSE_HZ        8000000u

#define CARDIA_SYSCLK_HZ     180000000u
#define CARDIA_HCLK_HZ       180000000u   /* AHB  /1  */
#define CARDIA_PCLK1_HZ      45000000u    /* APB1 /4  */
#define CARDIA_PCLK2_HZ      90000000u    /* APB2 /2  */

/* Timer clocks are NOT the bus clocks. See the comment block in
 * system_stm32f4xx.c -- when an APB prescaler is not 1, that bus's timers run
 * at twice PCLK. */
#define CARDIA_TIM_APB1_HZ   90000000u
#define CARDIA_TIM_APB2_HZ   180000000u

/* Set by SystemInit(). Read by the DWT helpers and printed at boot so the
 * reported cycle counts can be converted to time by anyone reading the log. */
extern uint32_t SystemCoreClock;

/* Configure flash latency, voltage scaling, over-drive, the PLL, the bus
 * prescalers, and switch SYSCLK to the PLL. Called from Reset_Handler before
 * main(). Leaves the core at CARDIA_SYSCLK_HZ. */
void SystemInit(void);

/* Crude busy-wait, calibrated from SystemCoreClock. Used only during bring-up
 * (settling a peripheral, spacing boot banner lines); nothing in the steady
 * state blocks on it. */
void system_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_SYSTEM_H */
