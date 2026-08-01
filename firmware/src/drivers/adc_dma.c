#include "adc_dma.h"

#include "gpio.h"
#include "rcc.h"
#include "stm32f446_regs.h"
#include "system.h"

static uint16_t *s_buf0;
static uint16_t *s_buf1;
static uint32_t  s_len;

static volatile uint32_t s_overruns;
static volatile uint32_t s_errors;
static volatile uint32_t s_half_ticks;
static volatile uint32_t s_blocks;

/* All stream-0 interrupt flags, in the low status/clear register. */
#define DMA_S0_ALL_FLAGS (DMA_LISR_FEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_TEIF0 \
                          | DMA_LISR_HTIF0 | DMA_LISR_TCIF0)

/* Default no-op so the driver is self-contained. Weak: the application's
 * definition replaces it at link time. */
__attribute__((weak))
void adc_dma_on_buffer(const uint16_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
}

void adc_dma_init(uint16_t *buf0, uint16_t *buf1, uint32_t len)
{
    s_buf0 = buf0;
    s_buf1 = buf1;
    s_len  = len;

    s_overruns = 0u;
    s_errors = 0u;
    s_half_ticks = 0u;
    s_blocks = 0u;

    rcc_enable_gpioa();
    rcc_enable_dma2();
    rcc_enable_adc1();

    /* PA0 to analog. gpio_init_board() already does this; repeated here so the
     * driver's precondition is stated where it is depended on. */
    gpio_set_mode(GPIOA, CARDIA_PIN_ECG, GPIO_MODE_ANALOG, GPIO_PULL_NONE,
                  GPIO_SPEED_LOW, GPIO_OTYPE_PUSHPULL, 0u);

    /* ------------------------------------------------------------------
     * DMA2 Stream 0, Channel 0.
     *
     * The stream/channel pair is not a free choice: RM0390 Table 28 maps ADC1's
     * request line to DMA2 Stream 0 Channel 0 (and Stream 4 Channel 0). Any
     * other combination simply never fires.
     * ------------------------------------------------------------------ */

    /* A stream must be fully disabled before it can be reprogrammed. EN does
     * not clear instantly -- the controller finishes any transfer in flight --
     * so poll it rather than assuming. */
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream0->CR & DMA_SxCR_EN) {
    }
    DMA2->LIFCR = DMA_S0_ALL_FLAGS;

    DMA2_Stream0->PAR  = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)buf0;
    DMA2_Stream0->M1AR = (uint32_t)buf1;
    DMA2_Stream0->NDTR = len;

    DMA2_Stream0->FCR = 0u;   /* direct mode: one transfer per request, no FIFO
                               * batching. At 360 Hz there is nothing to gain
                               * from bursting, and direct mode removes the FIFO
                               * threshold/burst-size interaction entirely. */

    DMA2_Stream0->CR =
          (0u << DMA_SxCR_CHSEL_Pos)   /* channel 0 = ADC1                    */
        | DMA_SxCR_DIR_P2M             /* peripheral -> memory                */
        | DMA_SxCR_MINC                /* walk the buffer                     */
        | DMA_SxCR_PSIZE_HALF          /* ADC_DR is 16-bit                    */
        | DMA_SxCR_MSIZE_HALF          /* buffers are uint16_t                */
        | DMA_SxCR_PL_HIGH             /* losing a sample is unrecoverable    */
        | DMA_SxCR_DBM                 /* double buffer: M0AR <-> M1AR        */
        | DMA_SxCR_TCIE                /* one buffer full                     */
        | DMA_SxCR_HTIE                /* mid-buffer deadline tick            */
        | DMA_SxCR_TEIE                /* transfer error                      */
        | DMA_SxCR_DMEIE;              /* direct mode error                   */

    /* PINC is deliberately clear: the source is one register, not an array.
     *
     * CIRC is deliberately not set: RM0390 9.3.10 says setting DBM forces
     * circular behaviour and the CIRC bit is ignored. Setting it as well would
     * suggest the two are independent, which they are not. */

    /* CT selects which of M0AR/M1AR the DMA writes next. Start on M0AR so the
     * first completed buffer is buf0 -- deterministic startup makes the first
     * few samples reproducible between runs, which matters for the HIL diff. */
    DMA2_Stream0->CR &= ~DMA_SxCR_CT;

    /* ------------------------------------------------------------------
     * ADC1.
     * ------------------------------------------------------------------ */

    /* ADCPRE = /4. PCLK2 is 90 MHz and the part's maximum ADC clock is 36 MHz,
     * so /2 would be out of spec; /4 gives 22.5 MHz. */
    ADC_COMMON->CCR = (ADC_COMMON->CCR & ~ADC_CCR_ADCPRE_Msk)
                    | ADC_CCR_ADCPRE_DIV4;

    /* 12-bit, no scan (a single channel), no end-of-conversion interrupt --
     * the DMA is the only consumer. */
    ADC1->CR1 = ADC_CR1_RES_12B;

    /* Sample time for channel 0: 84 ADC cycles. Total conversion is 84 + 12 =
     * 96 cycles = 4.3 us at 22.5 MHz, against a 2.78 ms sample period, so there
     * is no throughput pressure at all -- the long sample window is spent
     * charging the sample-and-hold capacitor through the source impedance of an
     * analogue front end, which is where accuracy actually comes from. */
    ADC1->SMPR2 = (ADC1->SMPR2 & ~ADC_SMPR2_SMP0_Msk)
                | (ADC_SMP_84CYC << ADC_SMPR2_SMP0_Pos);

    /* Regular sequence: length 1, first (and only) conversion is channel 0. */
    ADC1->SQR1 = 0u;                 /* L = 0 means one conversion */
    ADC1->SQR2 = 0u;
    ADC1->SQR3 = 0u;                 /* SQ1 = channel 0 */

    /* DDS keeps DMA requests coming after the transfer counter reloads. Without
     * it the ADC issues requests for exactly one buffer's worth and then stops
     * -- the classic "it captured the first 32 samples and died" symptom.
     *
     * CONT is deliberately clear: conversions are started by the TIM2 trigger,
     * one per edge. Continuous mode would free-run at 22.5 MHz / 96 cycles and
     * the 360 Hz timing would be meaningless. */
    ADC1->CR2 = ADC_CR2_DMA
              | ADC_CR2_DDS
              | ADC_CR2_EXTSEL_TIM2_TRGO
              | ADC_CR2_EXTEN_RISING;

    /* Interrupt priority: high, but numerically above (i.e. lower priority
     * than) nothing else -- this is the only interrupt in the system. Set
     * explicitly rather than left at the reset default so adding a second
     * interrupt later forces the question to be answered. */
    nvic_set_priority(DMA2_Stream0_IRQn, 1u);
    nvic_enable_irq(DMA2_Stream0_IRQn);
}

void adc_dma_start(void)
{
    DMA2->LIFCR = DMA_S0_ALL_FLAGS;
    DMA2_Stream0->NDTR = s_len;
    DMA2_Stream0->CR |= DMA_SxCR_EN;

    ADC1->CR2 |= ADC_CR2_ADON;
    /* The ADC needs t_STAB (about 3 us) after ADON before the first conversion
     * is valid. The first TIM2 edge is 2.78 ms away, but only if the caller
     * starts the timer after this returns -- so wait here and make it true
     * unconditionally. */
    system_delay_us(10u);
}

void adc_dma_stop(void)
{
    ADC1->CR2 &= ~ADC_CR2_ADON;
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream0->CR & DMA_SxCR_EN) {
    }
    DMA2->LIFCR = DMA_S0_ALL_FLAGS;
}

uint32_t adc_dma_overruns(void)   { return s_overruns; }
uint32_t adc_dma_errors(void)     { return s_errors; }
uint32_t adc_dma_half_ticks(void) { return s_half_ticks; }
uint32_t adc_dma_blocks(void)     { return s_blocks; }

void DMA2_Stream0_IRQHandler(void)
{
    const uint32_t status = DMA2->LISR;

    /* --- errors ---------------------------------------------------------
     * Transfer error means a bus fault on the AHB access; direct-mode error
     * means a request arrived while the previous one was outstanding. Both are
     * hardware-level and distinct from simply being too slow, so they get their
     * own counter. */
    if (status & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) {
        DMA2->LIFCR = DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0;
        ++s_errors;
    }

    /* --- half transfer --------------------------------------------------
     * Nothing to deliver; the buffer is half full. Counting it gives a liveness
     * signal that does not depend on beats being detected. */
    if (status & DMA_LISR_HTIF0) {
        DMA2->LIFCR = DMA_LISR_HTIF0;
        ++s_half_ticks;
    }

    /* --- transfer complete: one buffer is full --------------------------- */
    if (status & DMA_LISR_TCIF0) {
        DMA2->LIFCR = DMA_LISR_TCIF0;

        /* The swap has already happened by the time this runs. CT now names the
         * buffer the DMA is filling, so the completed one is the other. */
        const uint32_t ct_before = (DMA2_Stream0->CR & DMA_SxCR_CT) ? 1u : 0u;
        const uint16_t *completed = ct_before ? s_buf0 : s_buf1;

        adc_dma_on_buffer(completed, s_len);
        ++s_blocks;

        /* Exact overrun test, and the reason double buffering is worth having:
         * if CT changed while the callback was running, the DMA finished the
         * *next* buffer too, and the buffer just handed out was being rewritten
         * underneath the consumer. Reading CT costs one load and turns an
         * invisible data-corruption bug into a number. */
        const uint32_t ct_after = (DMA2_Stream0->CR & DMA_SxCR_CT) ? 1u : 0u;
        if (ct_after != ct_before) {
            ++s_overruns;
        }
    }
}
