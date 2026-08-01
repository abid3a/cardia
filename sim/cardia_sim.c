/* cardia_sim.c -- runs the firmware's signal chain on the host.
 *
 * This binary compiles the *same* .c files the MCU runs. Not a port, not a
 * reimplementation, not a Python model of the firmware -- the identical
 * translation units, with only the compiler changed. That is what lets the
 * project claim its measured accuracy applies to the device: the thing
 * evaluated against 49,691 held-out beats and the thing that will run on the
 * board are one piece of code.
 *
 * Usage:
 *   cardia_sim <samples.f32> [--beats <out.bin>] [--peaks <out.txt>]
 *
 * Input is raw little-endian float32 samples in millivolts at 360 Hz.
 * Output on stdout is a summary; the optional files carry the per-beat detail
 * the Python parity checker needs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pipeline.h"
#include "inference.h"

/* One pipeline instance, statically allocated -- same as the firmware, and it
 * makes `sizeof` a directly quotable RAM figure. */
static cardia_pipeline_t g_pipeline;
static cardia_pipeline_out_t g_out;

static float *read_samples(const char *path, size_t *n_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long bytes = ftell(f);
    rewind(f);
    if (bytes <= 0 || (bytes % (long)sizeof(float)) != 0) {
        fprintf(stderr, "%s: not a whole number of float32 samples\n", path);
        fclose(f);
        return NULL;
    }
    size_t n = (size_t)bytes / sizeof(float);
    float *buf = (float *)malloc(n * sizeof(float));
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, sizeof(float), n, f) != n) {
        fprintf(stderr, "%s: short read\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n_out = n;
    return buf;
}

/* Per-beat record written for the parity checker. Fields are written
 * individually rather than as a struct so there is no compiler-dependent
 * padding for the Python side to guess at. */
static void write_beat(FILE *f, const cardia_beat_result_t *r)
{
    uint32_t idx = r->r_index;
    uint32_t cls = r->aami_class;
    fwrite(&idx, sizeof(uint32_t), 1, f);
    fwrite(&cls, sizeof(uint32_t), 1, f);
    fwrite(r->rr, sizeof(float), CARDIA_N_RR_FEATURES, f);
    fwrite(r->logits, sizeof(int32_t), CARDIA_N_CLASSES, f);
    fwrite(r->beat, sizeof(float), CARDIA_BEAT_LEN, f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <samples.f32> [--beats out.bin] [--peaks out.txt]\n",
                argv[0]);
        return 2;
    }

    const char *beats_path = NULL;
    const char *peaks_path = NULL;
    for (int i = 2; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--beats") == 0) beats_path = argv[++i];
        else if (strcmp(argv[i], "--peaks") == 0) peaks_path = argv[++i];
    }

    size_t n = 0;
    float *x = read_samples(argv[1], &n);
    if (!x) return 1;

    FILE *fb = beats_path ? fopen(beats_path, "wb") : NULL;
    FILE *fp = peaks_path ? fopen(peaks_path, "w") : NULL;
    if (beats_path && !fb) { perror(beats_path); free(x); return 1; }
    if (peaks_path && !fp) { perror(peaks_path); free(x); return 1; }

    cardia_pipeline_init(&g_pipeline);

    unsigned long n_peaks = 0, n_beats = 0;
    unsigned long class_hist[CARDIA_N_CLASSES] = {0};

    for (size_t i = 0; i < n; ++i) {
        cardia_pipeline_step(&g_pipeline, x[i], &g_out);
        for (int k = 0; k < g_out.n_r; ++k) {
            n_peaks++;
            if (fp) fprintf(fp, "%u\n", (unsigned)g_out.r_index[k]);
        }
        if (g_out.have_result) {
            n_beats++;
            class_hist[g_out.result.aami_class]++;
            if (fb) write_beat(fb, &g_out.result);
        }
    }

    printf("samples %zu\n", n);
    printf("peaks %lu\n", n_peaks);
    printf("beats %lu\n", n_beats);
    for (int c = 0; c < CARDIA_N_CLASSES; ++c) {
        printf("class %s %lu\n", cardia_class_names[c], class_hist[c]);
    }
    printf("pipeline_state_bytes %zu\n", sizeof(cardia_pipeline_t));
    printf("inference_scratch_bytes %u\n", (unsigned)cardia_inference_scratch_bytes());

    if (fb) fclose(fb);
    if (fp) fclose(fp);
    free(x);
    return 0;
}
