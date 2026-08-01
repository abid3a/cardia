# Host unit tests. Plain make + cc, no framework, no submodules.
#
# The point of building the firmware's DSP and NN sources with the HOST
# compiler is that it makes them testable at all: a bug found here is found in
# a second, not by staring at a UART trace. Everything under src/dsp and
# src/nn is written with no MCU dependency precisely so this is possible.

CC      ?= cc
ROOT    := ..
SRCDIR  := $(ROOT)/firmware/src
BUILD   := build

CFLAGS  := -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
           -I$(SRCDIR)/dsp -I$(SRCDIR)/nn -I. \
           -ffp-contract=off
LDFLAGS := -lm

# -ffp-contract=off matters: without it the compiler may fuse a multiply and an
# add into a single FMA with only one rounding step. That is faster and more
# accurate, but it makes the host and the target disagree in the last bits and
# turns the parity test into a tolerance check. Determinism beats accuracy here.

DSP_SRC := $(SRCDIR)/dsp/biquad.c $(SRCDIR)/dsp/filter_coeffs.c \
           $(SRCDIR)/dsp/pan_tompkins.c $(SRCDIR)/dsp/pipeline.c
NN_SRC  := $(SRCDIR)/nn/nn_kernels.c $(SRCDIR)/nn/inference.c

# The generated model is only present after `make model`. Tests that need it
# are skipped gracefully when it is absent.
MODEL_SRC := $(wildcard $(SRCDIR)/nn/cardia_model.c)

COMMON := $(DSP_SRC) $(NN_SRC) $(MODEL_SRC)

TESTS := $(BUILD)/test_dsp $(BUILD)/test_nn

.PHONY: all run clean
all: $(TESTS)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/test_dsp: test_dsp.c test_vectors.h test_harness.h $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_dsp.c $(COMMON) $(LDFLAGS)

$(BUILD)/test_nn: test_nn.c test_harness.h $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_nn.c $(COMMON) $(LDFLAGS)

run: all
	@fail=0; for t in $(TESTS); do echo; ./$$t || fail=1; done; exit $$fail

clean:
	rm -rf $(BUILD)
