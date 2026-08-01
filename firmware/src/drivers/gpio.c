#include "gpio.h"

#include "rcc.h"

void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, gpio_mode_t mode,
                   gpio_pull_t pull, gpio_speed_t speed, gpio_otype_t otype,
                   uint32_t af)
{
    const uint32_t pin2 = pin * 2u;   /* two-bit fields */

    /* Alternate function first. MODER is written last, below, so the pin is
     * never briefly driving as an output with a stale AF selection. */
    if (mode == GPIO_MODE_AF) {
        const uint32_t idx   = pin >> 3;          /* AFR[0] = pins 0-7 */
        const uint32_t shift = (pin & 0x7u) * 4u; /* four-bit fields   */
        port->AFR[idx] = (port->AFR[idx] & ~(0xFUL << shift))
                       | ((af & 0xFUL) << shift);
    }

    port->OTYPER  = (port->OTYPER  & ~(0x1UL << pin))
                  | ((uint32_t)otype << pin);
    port->OSPEEDR = (port->OSPEEDR & ~(0x3UL << pin2))
                  | ((uint32_t)speed << pin2);
    port->PUPDR   = (port->PUPDR   & ~(0x3UL << pin2))
                  | ((uint32_t)pull << pin2);
    port->MODER   = (port->MODER   & ~(0x3UL << pin2))
                  | ((uint32_t)mode << pin2);
}

void gpio_init_board(void)
{
    rcc_enable_gpioa();

    /* ECG input. Analog mode disconnects the Schmitt trigger and the pull
     * resistors from the pad -- leaving either connected loads the source and
     * adds a leakage path that shows up as a slow DC drift on a high-impedance
     * biopotential front end. */
    gpio_set_mode(GPIOA, CARDIA_PIN_ECG, GPIO_MODE_ANALOG, GPIO_PULL_NONE,
                  GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL, 0u);

    /* Virtual COM port to the host. High speed rather than very high: 115200
     * baud needs nothing, and slower edges radiate less into the analogue
     * input sitting three pins away. */
    gpio_set_mode(GPIOA, CARDIA_PIN_TX, GPIO_MODE_AF, GPIO_PULL_NONE,
                  GPIO_SPEED_HIGH, GPIO_OTYPE_PUSHPULL, CARDIA_AF_USART2);
    /* RX is pulled up so an unconnected line idles at the mark level instead of
     * floating and generating framing errors. */
    gpio_set_mode(GPIOA, CARDIA_PIN_RX, GPIO_MODE_AF, GPIO_PULL_UP,
                  GPIO_SPEED_HIGH, GPIO_OTYPE_PUSHPULL, CARDIA_AF_USART2);

    /* LD2. Toggled once per classified beat, so the board's heartbeat is
     * visible without a terminal attached. */
    gpio_set_mode(GPIOA, CARDIA_PIN_LED, GPIO_MODE_OUTPUT, GPIO_PULL_NONE,
                  GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL, 0u);
    gpio_write(GPIOA, CARDIA_PIN_LED, 0);
}

void gpio_led_set(int on)
{
    gpio_write(GPIOA, CARDIA_PIN_LED, on);
}

void gpio_led_toggle(void)
{
    gpio_toggle(GPIOA, CARDIA_PIN_LED);
}
