#include "usart.h"

#include "gpio.h"
#include "rcc.h"
#include "stm32f446_regs.h"
#include "system.h"

#define CARDIA_BAUD 115200u

/* USART_BRR on the F4 is USARTDIV in 12.4 fixed point when OVER8 = 0, and
 * USARTDIV = PCLK / (16 * baud). Multiply both sides by 16 and the register
 * value is simply PCLK / baud, rounded:
 *
 *     45 000 000 / 115200 = 390.625 -> 391 = 0x187
 *     actual baud = 45e6 / (16 * 391/16) = 115 089  ->  -0.10% error
 *
 * UART framing tolerates about +/-2% per character, so 0.1% is comfortable.
 * Computing it here rather than hard-coding 0x187 means changing the APB1
 * prescaler cannot silently break the console. */
#define USART_BRR_VALUE  ((CARDIA_PCLK1_HZ + (CARDIA_BAUD / 2u)) / CARDIA_BAUD)

void usart_init(void)
{
    rcc_enable_usart2();

    /* Pins are configured by gpio_init_board(); calling it here as well is
     * harmless and makes this driver usable standalone during bring-up. */
    gpio_init_board();

    USART2->CR1 = 0u;               /* disable while reconfiguring */
    USART2->CR2 = USART_CR2_STOP_1BIT;
    USART2->CR3 = 0u;               /* no flow control, no DMA */
    USART2->BRR = USART_BRR_VALUE;

    /* 8 data bits (M = 0), no parity (PCE = 0), transmit and receive on. */
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    /* The first character after enabling can be corrupted if the line has not
     * settled at the idle level. One character time at 115200 is 87 us. */
    system_delay_us(200u);
}

void usart_putc(char c)
{
    while ((USART2->SR & USART_SR_TXE) == 0u) {
    }
    USART2->DR = (uint32_t)(uint8_t)c;
}

void usart_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        usart_putc(data[i]);
    }
}

void usart_puts(const char *s)
{
    while (*s != '\0') {
        usart_putc(*s++);
    }
}

void usart_flush(void)
{
    while ((USART2->SR & USART_SR_TC) == 0u) {
    }
}

void usart_put_u32(uint32_t v)
{
    /* 10 digits is the maximum for 2^32-1. Built backwards into a stack buffer,
     * so there is no division-by-descending-power table and no allocation. */
    char buf[10];
    int n = 0;

    if (v == 0u) {
        usart_putc('0');
        return;
    }
    while (v != 0u && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n-- > 0) {
        usart_putc(buf[n]);
    }
}

void usart_put_i32(int32_t v)
{
    if (v < 0) {
        usart_putc('-');
        /* Negate in unsigned space: -INT32_MIN is undefined in signed int32,
         * and the logit values printed here really can reach the extremes. */
        usart_put_u32((uint32_t)0u - (uint32_t)v);
    } else {
        usart_put_u32((uint32_t)v);
    }
}

void usart_put_hex32(uint32_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    usart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        usart_putc(digits[(v >> shift) & 0xFu]);
    }
}

void usart_put_fixed(float v, int decimals)
{
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;

    /* NaN is the only value not equal to itself. Catching it first means the
     * comparisons below cannot produce nonsense. */
    if (v != v) {
        usart_puts("nan");
        return;
    }

    int negative = 0;
    if (v < 0.0f) {
        negative = 1;
        v = -v;
    }

    uint32_t scale = 1u;
    for (int i = 0; i < decimals; ++i) {
        scale *= 10u;
    }

    /* Everything above this bound overflows uint32 once scaled. Say so rather
     * than wrapping into a small, believable, wrong number. */
    const float limit = 4.0e9f / (float)scale;
    if (v >= limit) {
        usart_puts(negative ? "-inf" : "inf");
        return;
    }

    const uint32_t scaled = (uint32_t)(v * (float)scale + 0.5f);
    const uint32_t whole = scaled / scale;
    const uint32_t frac = scaled - (whole * scale);

    if (negative) {
        usart_putc('-');
    }
    usart_put_u32(whole);

    if (decimals > 0) {
        usart_putc('.');
        /* Leading zeros in the fractional part are significant: 1.05 must not
         * print as "1.5". Emit them explicitly. */
        uint32_t divisor = scale / 10u;
        while (divisor > 0u) {
            usart_putc((char)('0' + ((frac / divisor) % 10u)));
            divisor /= 10u;
        }
    }
}

int usart_read_byte(uint8_t *out)
{
    const uint32_t sr = USART2->SR;

    if (sr & USART_SR_ORE) {
        /* ORE is cleared by reading SR then DR. Discarding the byte is correct:
         * it is the one that was overwritten, and the HIL protocol resynchro-
         * nises on its own framing rather than on byte position. */
        (void)USART2->DR;
        return 0;
    }
    if ((sr & USART_SR_RXNE) == 0u) {
        return 0;
    }

    *out = (uint8_t)(USART2->DR & 0xFFu);
    return 1;
}
