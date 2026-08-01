# firmware/Makefile -- convenience wrapper around the CMake target build.
#
# CMake is the build system; this exists so the common operations are one word
# long and so the toolchain paths from scripts/setup-toolchain.sh are found
# without anyone having to remember to export PATH. Everything here is a thin
# shell over `cmake` -- there is no build logic in this file, and none should be
# added, or the two will drift.
#
#   make            configure (if needed) and build
#   make size       flash/RAM breakdown
#   make hil        rebuild in hardware-in-the-loop mode
#   make flash      copy cardia.bin to a mounted NODE_F446RE
#   make clean      remove the build directory

REPO_ROOT   := $(abspath $(CURDIR)/..)
BUILD_DIR   ?= $(REPO_ROOT)/build/arm

# The setup script installs the ARM toolchain under $HOME/.local/arm and CMake +
# Ninja into the repo virtualenv. Prefer those; fall back to whatever is on PATH
# so a system-packaged toolchain also works.
ARM_TOOLCHAIN_DIR ?= $(HOME)/.local/arm/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin
VENV_BIN          := $(REPO_ROOT)/.venv/bin

ifneq ($(wildcard $(ARM_TOOLCHAIN_DIR)/arm-none-eabi-gcc),)
  export PATH := $(ARM_TOOLCHAIN_DIR):$(PATH)
endif

CMAKE := $(if $(wildcard $(VENV_BIN)/cmake),$(VENV_BIN)/cmake,cmake)
NINJA := $(if $(wildcard $(VENV_BIN)/ninja),$(VENV_BIN)/ninja,ninja)

TOOLCHAIN_FILE := $(CURDIR)/cmake/arm-none-eabi.cmake
SIZE           := arm-none-eabi-size
OBJDUMP        := arm-none-eabi-objdump

ELF := $(BUILD_DIR)/cardia.elf
BIN := $(BUILD_DIR)/cardia.bin
MAP := $(BUILD_DIR)/cardia.map

# Extra -D flags, e.g.  make CMAKE_ARGS=-DCARDIA_USE_CMSIS_NN=ON
CMAKE_ARGS ?=

.PHONY: all configure build hil size map disasm flash clean help

all: build

configure: $(BUILD_DIR)/build.ninja

$(BUILD_DIR)/build.ninja:
	$(CMAKE) -S $(CURDIR) -B $(BUILD_DIR) -G Ninja \
	  -DCMAKE_MAKE_PROGRAM=$(NINJA) \
	  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
	  $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

# HIL is a different binary, not a runtime switch, so it gets its own configure
# pass. Reconfiguring in place is cheap and keeps one build directory.
hil:
	$(CMAKE) -S $(CURDIR) -B $(BUILD_DIR) -G Ninja \
	  -DCMAKE_MAKE_PROGRAM=$(NINJA) \
	  -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
	  -DCARDIA_HIL_MODE=ON $(CMAKE_ARGS)
	$(CMAKE) --build $(BUILD_DIR)

size: build
	@$(SIZE) --format=berkeley $(ELF)
	@echo
	@$(SIZE) --format=sysv $(ELF) | head -n 20

# The map file is where a surprising flash figure is explained. Kept as a target
# rather than a habit so it is one command when the number moves.
map: build
	@echo "$(MAP)"

disasm: build
	@$(OBJDUMP) -d -S $(ELF)

# The Nucleo's ST-LINK enumerates as a USB mass-storage device; copying a raw
# binary onto it programs the flash. No debugger, no drivers, no udev rules --
# which is the whole reason this is the default path.
NODE_MOUNT ?= $(firstword $(wildcard /media/$(USER)/NODE_F446RE \
                                     /run/media/$(USER)/NODE_F446RE \
                                     /media/NODE_F446RE \
                                     /Volumes/NODE_F446RE))

flash: build
	@if [ -z "$(NODE_MOUNT)" ]; then \
	  echo "NODE_F446RE not mounted."; \
	  echo "Plug the board in, or pass the path:  make flash NODE_MOUNT=/path/to/NODE_F446RE"; \
	  exit 1; \
	fi
	cp $(BIN) $(NODE_MOUNT)/
	@sync
	@echo "Flashed $(BIN) -> $(NODE_MOUNT)"

# Alternative when openocd is available -- gives verification and a reset,
# which the mass-storage path does not:
#
#   openocd -f board/st_nucleo_f4.cfg \
#           -c "program $(ELF) verify reset exit"
#
# Or with the ST tools:
#
#   st-flash --reset write $(BIN) 0x08000000

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make          configure + build  -> $(ELF)"
	@echo "make hil      build with CARDIA_HIL_MODE=ON"
	@echo "make size     flash/RAM breakdown"
	@echo "make map      path to the link map"
	@echo "make disasm   annotated disassembly"
	@echo "make flash    copy .bin to a mounted NODE_F446RE"
	@echo "make clean    remove $(BUILD_DIR)"
