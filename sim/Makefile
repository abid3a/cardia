# Host simulator. Builds the firmware's own sources with the host compiler.

CC     ?= cc
ROOT   := ..
SRCDIR := $(ROOT)/firmware/src
BUILD  := build

CFLAGS := -std=c11 -O2 -g -Wall -Wextra \
          -I$(SRCDIR)/dsp -I$(SRCDIR)/nn \
          -ffp-contract=off
LDFLAGS := -lm

SRC := cardia_sim.c \
       $(SRCDIR)/dsp/biquad.c $(SRCDIR)/dsp/filter_coeffs.c \
       $(SRCDIR)/dsp/pan_tompkins.c $(SRCDIR)/dsp/pipeline.c \
       $(SRCDIR)/nn/nn_kernels.c $(SRCDIR)/nn/inference.c \
       $(SRCDIR)/nn/cardia_model.c

.PHONY: all clean
all: $(BUILD)/cardia_sim

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/cardia_sim: $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -rf $(BUILD)
