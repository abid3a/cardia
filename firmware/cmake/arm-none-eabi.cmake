# arm-none-eabi.cmake -- CMake toolchain file for the STM32F446RE target build.
#
# Usage:
#   cmake -S firmware -B build/arm -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# The compiler-identification step CMake runs before anything else tries to
# produce an executable. On a bare-metal target that link fails -- there is no
# crt0 entry point, no default linker script, no heap -- and CMake reports the
# compiler as broken even though it is fine. Telling it to test with a static
# library instead makes the probe succeed and costs nothing: the real link is
# still exercised by the actual build.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Allow an explicit prefix (CI, or a non-standard install) but otherwise search
# PATH. The setup script installs into ~/.local/arm/..., which callers add to
# PATH or point at with ARM_TOOLCHAIN_DIR.
if(DEFINED ENV{ARM_TOOLCHAIN_DIR})
  set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()

if(ARM_TOOLCHAIN_DIR)
  set(_arm_hint "${ARM_TOOLCHAIN_DIR}")
else()
  set(_arm_hint "")
endif()

find_program(ARM_CC      arm-none-eabi-gcc     HINTS ${_arm_hint} REQUIRED)
find_program(ARM_CXX     arm-none-eabi-g++     HINTS ${_arm_hint})
find_program(ARM_ASM     arm-none-eabi-gcc     HINTS ${_arm_hint})
find_program(ARM_OBJCOPY arm-none-eabi-objcopy HINTS ${_arm_hint} REQUIRED)
find_program(ARM_OBJDUMP arm-none-eabi-objdump HINTS ${_arm_hint})
find_program(ARM_SIZE    arm-none-eabi-size    HINTS ${_arm_hint} REQUIRED)
find_program(ARM_AR      arm-none-eabi-gcc-ar  HINTS ${_arm_hint})
find_program(ARM_RANLIB  arm-none-eabi-gcc-ranlib HINTS ${_arm_hint})

set(CMAKE_C_COMPILER   "${ARM_CC}")
set(CMAKE_ASM_COMPILER "${ARM_ASM}")
if(ARM_CXX)
  set(CMAKE_CXX_COMPILER "${ARM_CXX}")
endif()
if(ARM_AR)
  set(CMAKE_AR "${ARM_AR}")
endif()
if(ARM_RANLIB)
  set(CMAKE_RANLIB "${ARM_RANLIB}")
endif()

set(CMAKE_OBJCOPY "${ARM_OBJCOPY}" CACHE FILEPATH "arm-none-eabi-objcopy")
set(CMAKE_OBJDUMP "${ARM_OBJDUMP}" CACHE FILEPATH "arm-none-eabi-objdump")
set(CMAKE_SIZE    "${ARM_SIZE}"    CACHE FILEPATH "arm-none-eabi-size")

# Never look in the host sysroot for headers or libraries.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
