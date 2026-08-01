/* usart.h -- USART2 at 115200 8N1, the board's only I/O channel.
 *
 * PA2/PA3 are wired to the ST-LINK's virtual COM port on the Nucleo, so this
 * comes out of the same USB cable that powers and flashes the board -- no extra
 * adapter, and the host-side HIL script talks to /dev/ttyACM0.
 *
 * There is no printf here, and that is a deliberate constraint rather than an
 * omission. Linking newlib's printf costs roughly 8-12 KB of flash, pulls in
 * malloc (which this firmware is built to prove it does not need -- see the
 * heap note in the linker script), and has a stack footprint large enough to
 * matter against a 4 KB reservation. Everything this program needs to emit is a
 * decimal integer, a fixed-point float, or a literal string; the four helpers
 * below cover that in a few hundred bytes with a bounded, known stack cost.
 */

#ifndef CARDIA_USART_H
#define CARDIA_USART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure PA2/PA3, the clock, and the baud divider. Requires SystemInit() to
 * have run: the divider is computed from CARDIA_PCLK1_HZ. */
void usart_init(void);

/* --- transmit (blocking) -------------------------------------------------
 * Blocking on TXE is the right trade here. The only thing this program prints
 * is one line per beat -- at most ~4 lines/second at a tachycardic 240 bpm,
 * about 60 bytes each, which is 5 ms of line time per second, or 0.5% duty. An
 * interrupt-driven ring buffer would add an ISR, a buffer, and an overflow
 * policy to save an amount of CPU that does not exist. Blocking also means the
 * output is never silently dropped, which matters when the output IS the
 * experimental result.
 */
void usart_putc(char c);
void usart_write(const char *data, size_t len);
void usart_puts(const char *s);          /* NUL-terminated, no newline added */

/* --- formatting ---------------------------------------------------------- */
void usart_put_u32(uint32_t v);
void usart_put_i32(int32_t v);
void usart_put_hex32(uint32_t v);

/* Fixed-point float, `decimals` digits after the point (0..6). Rounds
 * half-away-from-zero. Prints "nan"/"inf"/"ovf" rather than producing a
 * plausible wrong number for values it cannot represent. */
void usart_put_fixed(float v, int decimals);

/* --- receive (non-blocking) ----------------------------------------------
 * Returns 1 and stores a byte if RXNE is set, 0 otherwise. Polled rather than
 * interrupt-driven because the only consumer is the HIL sample feeder, which
 * has nothing else to do while it waits. Overrun is cleared silently: at 115200
 * the host cannot outrun a 180 MHz core reading one byte per loop, so an ORE
 * means the link was interrupted, and the HIL protocol's own framing recovers.
 */
int usart_read_byte(uint8_t *out);

/* Block until the transmit shift register has drained. Used before entering a
 * mode change or a halt so the last line is not truncated. */
void usart_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* CARDIA_USART_H */
