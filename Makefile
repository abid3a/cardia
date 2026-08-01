# Cardia -- top-level convenience targets.
#
# Everything here is a thin wrapper over the real build files so that a fresh
# clone has one obvious entry point per task.

PY := .venv/bin/python
ARM_ROOT := $(HOME)/.local/arm/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi

.PHONY: help setup data train export test sim firmware size eval clean all

help:
	@echo "Cardia targets:"
	@echo "  make setup      install toolchain + Python venv (no root needed)"
	@echo "  make data       download MIT-BIH and build the DS1/DS2 caches"
	@echo "  make train      float -> BatchNorm fold -> int8 QAT"
	@echo "  make export     generate firmware/src/nn/cardia_model.{h,c}"
	@echo "  make test       host unit tests"
	@echo "  make sim        build the host simulator"
	@echo "  make parity     run the simulator over DS2 and check C/Python parity"
	@echo "  make eval       final inter-patient vs intra-patient evaluation"
	@echo "  make firmware   cross-compile for the STM32F446RE"
	@echo "  make size       flash/RAM breakdown from the map file"
	@echo "  make all        generate -> test -> sim -> firmware"

setup:
	./scripts/setup-toolchain.sh

data:
	$(PY) ml/scripts/download_data.py

# The generated headers are checked in, but regenerating them must be a no-op.
# CI runs this and fails if the working tree changes, which is what stops a
# constant from being edited in Python and silently not reaching the C.
generate:
	$(PY) ml/scripts/gen_config_header.py
	$(PY) ml/scripts/gen_filter_coeffs.py
	$(PY) ml/scripts/gen_test_vectors.py

train:
	$(PY) ml/scripts/run_train.py

export:
	$(PY) ml/scripts/export_model.py

test:
	$(MAKE) -C tests run

sim:
	$(MAKE) -C sim

parity: sim
	$(PY) sim/run_sim.py --records ds2

eval:
	$(PY) ml/scripts/run_eval.py

firmware:
	PATH="$(ARM_ROOT)/bin:$(PWD)/.venv/bin:$$PATH" $(MAKE) -C firmware

size:
	PATH="$(ARM_ROOT)/bin:$(PWD)/.venv/bin:$$PATH" $(MAKE) -C firmware size

all: generate test sim firmware

clean:
	$(MAKE) -C tests clean
	$(MAKE) -C sim clean
	$(MAKE) -C firmware clean
