/* main.c -- Cardia on the STM32F446RE.
 *
 * Two builds from one source, selected by -DCARDIA_HIL_MODE=1:
 *
 *   LIVE (default)
 *       TIM2 triggers ADC1_IN0 at 360 Hz, DMA2 Stream 0 ping-pongs blocks into
 *       RAM, and each block is converted to millivolts and pushed through the
 *       pipeline. This is the product.
 *
 *   HIL (hardware-in-the-loop)
 *       Samples arrive over USART2 as little-endian int16 instead of from the
 *       ADC. Everything downstream is byte-identical to the live path.
 *
 * Why the HIL mode exists, since it is the more interesting half
 * -------------------------------------------------------------
 * The claim this project has to support is "the board runs the model that was
 * evaluated." Measuring accuracy on the board directly is impossible: the
 * MIT-BIH annotations are on a specific 30-minute recording, and there is no
 * way to play that recording into an analogue input with enough fidelity to
 * know that a disagreement came from the model rather than from the signal
 * generator, the cabling, or the ADC's noise floor.
 *
 * So the analogue path is taken out of the argument. The host streams the exact
 * int16 samples the simulator consumed; the board runs the identical DSP and
 * inference code on them and prints its results; the two output streams are
 * diffed. Any difference is a real difference -- a compiler doing something
 * different with a float expression, an uninitialised buffer, an int width that
 * behaves differently on a 32-bit target. That diff is the correctness gate,
 * and it is a yes/no answer rather than a tolerance.
 *
 * (Note the units differ between modes: live feeds millivolts, HIL feeds the
 * host's raw sample units. This is intentional and harmless -- the beat window
 * is z-scored per beat and Pan-Tompkins' thresholds are adaptive, so both are
 * invariant to a constant gain. What must NOT differ is the sample sequence,
 * and it does not.)
 */

#include <stdint.h>
#include <stddef.h>

#include "cardia_config.h"
#include "pipeline.h"
#include "inference.h"
#include "cardia_model.h"

#include "stm32f446_regs.h"
#include "system.h"
#include "adc_dma.h"
#include "dwt.h"
#include "gpio.h"
#include "tim.h"
#include "usart.h"

#ifndef CARDIA_HIL_MODE
#define CARDIA_HIL_MODE 0
#endif

/* --- acquisition block size ----------------------------------------------
 * 32 samples per DMA buffer = 88.9 ms of ECG per interrupt, ~11 interrupts per
 * second. Small enough that the added latency is negligible against the
 * pipeline's own ~800 ms (a beat cannot be classified until the following R
 * peak supplies its post-RR feature), large enough that interrupt overhead is
 * invisible. Two buffers of 64 bytes each. */
#define CARDIA_ADC_BLOCK 32u

/* --- ADC scaling ----------------------------------------------------------
 * 12-bit conversion against VREF+ = 3.3 V, so one count is 3300/4096 mV. The
 * front end is single-supply and biases the signal to mid-rail, so mid-scale is
 * subtracted to recover a signed millivolt value centred on zero. */
#define CARDIA_ADC_MV_PER_COUNT (3300.0f / 4096.0f)
#define CARDIA_ADC_MID_MV       1650.0f

/* Iterations for the startup latency benchmark. 64 runs of a deterministic
 * routine: enough for the spread from flash wait states and cache state to show
 * up, quick enough (a few milliseconds) not to delay the boot banner. */
#define CARDIA_BENCH_ITERS 64u

/* ==========================================================================
 * Static state
 *
 * Every buffer below is a file-scope object. Nothing is allocated, and nothing
 * large is on the stack: cardia_pipeline_t alone is several kilobytes against a
 * 4 KB stack reservation, and cardia_pipeline_out_t carries a 256-float beat
 * window. Putting either in an automatic variable would overflow into .bss and
 * corrupt exactly the state being processed.
 * ======================================================================== */

static cardia_pipeline_t     g_pipeline;
static cardia_pipeline_out_t g_out;

/* Cycles spent inside the pipeline step that produced the most recent
 * classification -- i.e. filtering plus detection plus the full int8 network,
 * for the one sample where all of that fires. */
static uint32_t g_last_beat_cycles;

/* The acquisition buffers and the ISR handoff exist only in the live build.
 * Declaring them unconditionally would leave 128 bytes of DMA buffer in a HIL
 * image that has no DMA, and -Wunused-variable would rightly complain. */
#if !CARDIA_HIL_MODE

static uint16_t g_adc_buf0[CARDIA_ADC_BLOCK];
static uint16_t g_adc_buf1[CARDIA_ADC_BLOCK];

/* Handoff from the DMA interrupt to the main loop. The ISR publishes a pointer
 * and bumps a sequence number; the main loop compares sequence numbers. A
 * counter rather than a flag so a missed block is visible as a gap rather than
 * silently coalescing into one. */
static const uint16_t *volatile g_ready_buf;
static volatile uint32_t g_ready_len;
static volatile uint32_t g_ready_seq;
static uint32_t g_done_seq;

#endif

/* ==========================================================================
 * Output
 * ======================================================================== */

static void emit_beat(const cardia_beat_result_t *r, uint32_t cycles)
{
    /* BEAT <r_index> <class> <cycles> <l0> <l1> <l2> <l3> <l4>
     *
     * Space separated, one line per beat, no framing and no timestamps. The
     * host-side diff splits on whitespace, and the class is a single letter so
     * a column of output is readable by eye as well as by script. The five
     * logits are included because a disagreement between host and target is far
     * easier to localise when the margin is visible: two implementations that
     * pick different classes with near-identical logits have a tie-break
     * problem, and two with wildly different logits have an arithmetic bug. */
    usart_puts("BEAT ");
    usart_put_u32(r->r_index);
    usart_putc(' ');
    usart_putc(cardia_class_names[r->aami_class][0]);
    usart_putc(' ');
    usart_put_u32(cycles);
    for (int i = 0; i < CARDIA_N_CLASSES; ++i) {
        usart_putc(' ');
        usart_put_i32(r->logits[i]);
    }
    usart_putc('\n');
}

static void emit_kv_u32(const char *key, uint32_t value)
{
    usart_puts(key);
    usart_putc(' ');
    usart_put_u32(value);
    usart_putc('\n');
}

/* ==========================================================================
 * Startup benchmark
 * ======================================================================== */

/* A synthetic beat with a plausible QRS: a narrow triangular spike on a slow
 * baseline. The waveform's realism does not matter -- the network's execution
 * time is data-independent, since every layer is a fixed loop nest with no
 * early exits -- but a constant-zero input would be an unconvincing benchmark
 * to anyone reading the number, and would risk being optimised differently.
 * Built with integer arithmetic so it is identical on every build. */
static void build_synthetic_beat(float *beat, float *rr)
{
    for (int i = 0; i < CARDIA_BEAT_LEN; ++i) {
        /* Slow baseline: a triangle wave over the whole window, +/-0.2. */
        const int tri = (i < CARDIA_BEAT_LEN / 2)
                            ? i
                            : (CARDIA_BEAT_LEN - i);
        float v = ((float)tri / (float)(CARDIA_BEAT_LEN / 2)) * 0.4f - 0.2f;

        /* QRS: a 15-sample spike centred on the R position the windowing uses. */
        const int d = i - CARDIA_BEAT_PRE;
        const int ad = (d < 0) ? -d : d;
        if (ad < 8) {
            v += (float)(8 - ad) * 0.45f;
        }
        beat[i] = v;
    }

    /* Normal-sinus-like timing: 0.8 s intervals, ratios of 1. */
    rr[0] = 0.8f;
    rr[1] = 0.8f;
    rr[2] = 1.0f;
    rr[3] = 1.0f;
}

static void bench_inference(void)
{
    static float beat[CARDIA_BEAT_LEN];
    static float rr[CARDIA_N_RR_FEATURES];
    static int32_t logits[CARDIA_N_CLASSES];

    build_synthetic_beat(beat, rr);

    /* One untimed call first. The first execution pays for filling the
     * instruction cache and the flash prefetch buffer with the network's code
     * and the first weights it touches; including it would report a worst case
     * that only ever happens once and would make the mean misleading. The
     * min/max spread below is then the honest steady-state variation. */
    (void)cardia_classify(beat, rr, logits);

    uint32_t min_cyc = 0xFFFFFFFFu;
    uint32_t max_cyc = 0u;
    uint64_t sum_cyc = 0u;

    for (uint32_t i = 0; i < CARDIA_BENCH_ITERS; ++i) {
        const uint32_t t0 = dwt_start();
        (void)cardia_classify(beat, rr, logits);
        const uint32_t cyc = dwt_elapsed(t0, dwt_stop());

        if (cyc < min_cyc) min_cyc = cyc;
        if (cyc > max_cyc) max_cyc = cyc;
        sum_cyc += cyc;
    }

    const uint32_t mean_cyc = (uint32_t)(sum_cyc / CARDIA_BENCH_ITERS);

    /* BENCH <iters> <min_cyc> <mean_cyc> <max_cyc> <min_us> <mean_us> <max_us>
     * Microseconds are printed to three decimals from the nanosecond helper, so
     * a 50 us inference does not round to "50" and hide its variation. */
    usart_puts("BENCH ");
    usart_put_u32(CARDIA_BENCH_ITERS);
    usart_putc(' ');
    usart_put_u32(min_cyc);
    usart_putc(' ');
    usart_put_u32(mean_cyc);
    usart_putc(' ');
    usart_put_u32(max_cyc);
    usart_putc(' ');
    usart_put_fixed((float)dwt_cycles_to_ns(min_cyc) / 1000.0f, 3);
    usart_putc(' ');
    usart_put_fixed((float)dwt_cycles_to_ns(mean_cyc) / 1000.0f, 3);
    usart_putc(' ');
    usart_put_fixed((float)dwt_cycles_to_ns(max_cyc) / 1000.0f, 3);
    usart_putc('\n');

    /* What fraction of the real-time budget one beat costs. At 240 bpm a beat
     * arrives every 250 ms; this is the inference alone against that. */
    usart_puts("BUDGET_PPM ");
    usart_put_u32((uint32_t)((uint64_t)dwt_cycles_to_ns(max_cyc) * 1000000u
                             / 250000000u));
    usart_putc('\n');
}

/* ==========================================================================
 * Resource banner
 * ======================================================================== */

static void emit_banner(void)
{
    usart_puts("\nCARDIA\n");
#if CARDIA_HIL_MODE
    usart_puts("MODE hil\n");
#else
    usart_puts("MODE live\n");
#endif

    emit_kv_u32("SYSCLK_HZ", SystemCoreClock);
    emit_kv_u32("FS_HZ", (uint32_t)CARDIA_FS_HZ);
    emit_kv_u32("TIM_CLOCK_HZ", CARDIA_TIM_CLOCK_HZ);

    /* The RAM/flash budget, measured rather than estimated. These three numbers
     * plus the .map file account for essentially all of the firmware's
     * footprint, and they are printed by the device itself so a claim in the
     * documentation can be checked against the board in front of you. */
    emit_kv_u32("NN_SCRATCH_BYTES", cardia_inference_scratch_bytes());
    emit_kv_u32("MODEL_PARAM_BYTES", (uint32_t)CARDIA_MODEL_PARAM_BYTES);
    emit_kv_u32("PIPELINE_BYTES", (uint32_t)sizeof(cardia_pipeline_t));
    emit_kv_u32("ADC_BLOCK", CARDIA_ADC_BLOCK);
}

/* ==========================================================================
 * Shared sample path
 * ======================================================================== */

/* One sample in, zero or one classification out. Both modes call exactly this,
 * which is the point: there is no separate "test" code path that could pass
 * while the real one fails. */
static void feed_sample(float sample)
{
    const uint32_t t0 = dwt_start();
    const int classified = cardia_pipeline_step(&g_pipeline, sample, &g_out);
    const uint32_t cycles = dwt_elapsed(t0, dwt_stop());

    if (classified && g_out.have_result) {
        g_last_beat_cycles = cycles;
        emit_beat(&g_out.result, cycles);
        gpio_led_toggle();
    }
}

/* ==========================================================================
 * Live mode
 * ======================================================================== */

#if !CARDIA_HIL_MODE

/* Interrupt context. Deliberately does nothing but publish a pointer: the
 * pipeline step can run a full int8 inference, and doing that inside the ISR
 * would hold off the next DMA interrupt and, worse, hold off the blocking UART
 * writes it triggers. The driver's overrun counter reports if the main loop
 * ever fails to keep up. */
void adc_dma_on_buffer(const uint16_t *buf, uint32_t len)
{
    g_ready_buf = buf;
    g_ready_len = len;
    ++g_ready_seq;
}

static void run_live(void)
{
    gpio_init_board();
    tim2_trigger_init();
    adc_dma_init(g_adc_buf0, g_adc_buf1, CARDIA_ADC_BLOCK);
    adc_dma_start();
    tim2_start();

    usart_puts("READY live\n");

    uint32_t last_report = 0u;

    for (;;) {
        const uint32_t seq = g_ready_seq;

        if (seq == g_done_seq) {
            /* Nothing to do until the next block. WFI parks the core until an
             * interrupt arrives instead of spinning; it also means the idle
             * current draw is a real number rather than "whatever a busy loop
             * costs". */
            cardia_wfi();
            continue;
        }

        /* If more than one block arrived while we were working, the sequence
         * gap is the count. The samples in the skipped block are gone -- the
         * DMA has already overwritten them -- so there is nothing to recover;
         * what matters is that it is recorded rather than silently absorbed. */
        const uint16_t *buf = g_ready_buf;
        const uint32_t len = g_ready_len;
        g_done_seq = seq;

        for (uint32_t i = 0; i < len; ++i) {
            const float mv = ((float)buf[i] * CARDIA_ADC_MV_PER_COUNT)
                           - CARDIA_ADC_MID_MV;
            feed_sample(mv);
        }

        /* Health line roughly once a minute (360 Hz / 32 = 11.25 blocks/s).
         * Cheap, and it makes a quiet channel distinguishable from a dead one:
         * a recording with no detected beats and a healthy STAT line is a
         * lead-off problem, not a firmware problem. */
        if ((seq - last_report) >= 675u) {
            last_report = seq;
            usart_puts("STAT ");
            usart_put_u32(adc_dma_blocks());
            usart_putc(' ');
            usart_put_u32(adc_dma_overruns());
            usart_putc(' ');
            usart_put_u32(adc_dma_errors());
            usart_putc(' ');
            usart_put_u32(g_last_beat_cycles);
            usart_putc('\n');
        }
    }
}

#else /* CARDIA_HIL_MODE */

/* ==========================================================================
 * HIL mode
 * ======================================================================== */

static void run_hil(void)
{
    gpio_init_board();

    usart_puts("READY hil\n");

    /* Two-byte little-endian int16 per sample, no framing. Framing was
     * considered and rejected: the link is a direct USB CDC pipe with no lossy
     * medium in it, the host sends a fixed-length stream and then stops, and
     * any desynchronisation shows up immediately as garbage in the diff rather
     * than being silently corrected. Adding a sync word would hide exactly the
     * failure the test is looking for. */
    uint8_t lo = 0u;
    int have_low = 0;

    for (;;) {
        uint8_t byte;
        if (!usart_read_byte(&byte)) {
            continue;
        }

        if (!have_low) {
            lo = byte;
            have_low = 1;
            continue;
        }
        have_low = 0;

        const uint16_t raw = (uint16_t)((uint16_t)byte << 8) | (uint16_t)lo;
        const int16_t sample = (int16_t)raw;

        /* Passed through as-is, in the host's units. See the note at the top of
         * this file: the pipeline is amplitude-adaptive, and preserving the
         * exact sample sequence is what the parity test needs. */
        feed_sample((float)sample);
    }
}

#endif /* CARDIA_HIL_MODE */

/* ==========================================================================
 * Entry
 * ======================================================================== */

int main(void)
{
    /* SystemInit() has already run from Reset_Handler: the core is at 180 MHz
     * with the flash and regulator configured for it. */
    dwt_init();
    usart_init();

    emit_banner();
    bench_inference();

    cardia_pipeline_init(&g_pipeline);

    cardia_irq_enable();

#if CARDIA_HIL_MODE
    run_hil();
#else
    run_live();
#endif

    /* Neither run_* returns. */
    for (;;) {
    }
}
