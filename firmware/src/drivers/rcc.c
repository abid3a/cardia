#include "rcc.h"

#include "stm32f446_regs.h"

/* Enabling a clock takes effect a bus cycle or two after the write retires. A
 * peripheral register written in the very next instruction can therefore miss.
 * Reading the enable register back inserts exactly the required stall and is
 * the idiom ST's own headers use. */
#define RCC_ENABLE(reg, bit)     \
    do {                         \
        RCC->reg |= (bit);       \
        (void)RCC->reg;          \
    } while (0)

void rcc_enable_gpioa(void)  { RCC_ENABLE(AHB1ENR, RCC_AHB1ENR_GPIOAEN); }
void rcc_enable_gpiob(void)  { RCC_ENABLE(AHB1ENR, RCC_AHB1ENR_GPIOBEN); }
void rcc_enable_gpioc(void)  { RCC_ENABLE(AHB1ENR, RCC_AHB1ENR_GPIOCEN); }
void rcc_enable_dma2(void)   { RCC_ENABLE(AHB1ENR, RCC_AHB1ENR_DMA2EN); }
void rcc_enable_adc1(void)   { RCC_ENABLE(APB2ENR, RCC_APB2ENR_ADC1EN); }
void rcc_enable_tim2(void)   { RCC_ENABLE(APB1ENR, RCC_APB1ENR_TIM2EN); }
void rcc_enable_usart2(void) { RCC_ENABLE(APB1ENR, RCC_APB1ENR_USART2EN); }
void rcc_enable_pwr(void)    { RCC_ENABLE(APB1ENR, RCC_APB1ENR_PWREN); }
