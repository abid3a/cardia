/* gpio.h -- one helper that programs all five per-pin GPIO fields at once.
 *
 * The F4 splits a pin's configuration across MODER, OTYPER, OSPEEDR, PUPDR and
 * one of the two AFR words. Configuring a pin as five separate read-modify-
 * writes scattered through an init function is how pins end up half-configured:
 * the mode is set but the alternate function is left at 0, and USART2_TX drives
 * a steady low instead of data. Doing all five together, in one place, makes
 * that class of mistake structurally impossible.
 */

#ifndef CARDIA_GPIO_H
#define CARDIA_GPIO_H

#include <stdint.h>

#include "stm32f446_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPIO_MODE_INPUT  = 0x0,
    GPIO_MODE_OUTPUT = 0x1,
    GPIO_MODE_AF     = 0x2,
    GPIO_MODE_ANALOG = 0x3
} gpio_mode_t;

typedef enum {
    GPIO_PULL_NONE = 0x0,
    GPIO_PULL_UP   = 0x1,
    GPIO_PULL_DOWN = 0x2
} gpio_pull_t;

typedef enum {
    GPIO_SPEED_LOW      = 0x0,
    GPIO_SPEED_MEDIUM   = 0x1,
    GPIO_SPEED_HIGH     = 0x2,
    GPIO_SPEED_VERYHIGH = 0x3
} gpio_speed_t;

typedef enum {
    GPIO_OTYPE_PUSHPULL  = 0x0,
    GPIO_OTYPE_OPENDRAIN = 0x1
} gpio_otype_t;

/* Configure one pin completely. `af` is ignored unless mode is GPIO_MODE_AF. */
void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, gpio_mode_t mode,
                   gpio_pull_t pull, gpio_speed_t speed, gpio_otype_t otype,
                   uint32_t af);

/* BSRR is used rather than a read-modify-write of ODR: it is a single atomic
 * store, so a pin write in an ISR cannot corrupt one in progress in main. */
static inline void gpio_write(GPIO_TypeDef *port, uint32_t pin, int level)
{
    port->BSRR = level ? (1UL << pin) : (1UL << (pin + 16u));
}

static inline void gpio_toggle(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = (port->ODR & (1UL << pin)) ? (1UL << (pin + 16u)) : (1UL << pin);
}

static inline int gpio_read(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR & (1UL << pin)) ? 1 : 0;
}

/* --- Nucleo-F446RE board pins --------------------------------------------
 *   PA0  ECG analogue input          ADC1_IN0
 *   PA2  USART2_TX -> ST-LINK VCP    AF7
 *   PA3  USART2_RX <- ST-LINK VCP    AF7
 *   PA5  LD2, the green user LED     output push-pull
 * (PA5 is shared with SPI1_SCK on the Arduino header; nothing here uses SPI1.)
 */
#define CARDIA_PIN_ECG     0u
#define CARDIA_PIN_TX      2u
#define CARDIA_PIN_RX      3u
#define CARDIA_PIN_LED     5u
#define CARDIA_AF_USART2   7u

void gpio_init_board(void);
void gpio_led_set(int on);
void gpio_led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_GPIO_H */
