# Toolchain

Cardia builds two ways from one source tree:

| Track | Compiler | Output |
|---|---|---|
| Host (tests, simulator, parity check) | system `gcc` / `clang` | native binaries in `build/` |
| Target (STM32F446RE) | `arm-none-eabi-gcc` | `cardia.elf` / `.bin` / `.map` |

The DSP and inference sources are identical between the two. Only
`firmware/src/drivers/` and the startup/linker files are target-only.

## One-shot setup

```bash
./scripts/setup-toolchain.sh
```

That script was written under a real constraint worth stating, because it
shapes every choice in it: **the development machine has no root access.**
No `sudo apt install`. Everything therefore installs into `$HOME/.local` or
into the repo, and nothing touches system packages.

## What it installs, and why that way

### ARM GNU bare-metal toolchain

`arm-none-eabi-gcc` 14.2.Rel1, downloaded as the official tarball from
`developer.arm.com` and extracted to `~/.local/arm/`. Add to PATH:

```bash
export PATH="$HOME/.local/arm/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi/bin:$PATH"
arm-none-eabi-gcc --version   # -> 14.2.1 20241119
```

If `developer.arm.com` is unreachable the script falls back to the
[xPack](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack) GitHub
release, which packages the same upstream GCC.

The bare-metal (`arm-none-eabi`) triple rather than a Linux one: there is no OS
on the target, so the toolchain must default to newlib and must not assume a
dynamic loader, a libc with syscalls, or an ELF interpreter.

### CMake + Ninja

```bash
pip install cmake ninja
```

The Python wheels for CMake and Ninja ship real, working native binaries. This
is the cleanest root-free way to get a modern CMake — distribution packages are
usually old and installing them needs `sudo`. Versions used: CMake 4.4.0,
Ninja 1.13.0.

### Python virtualenv

`python3 -m venv .venv`, then `pip install -r ml/requirements.txt`.

One wrinkle worth recording because it will bite anyone reproducing this on
Debian/Ubuntu: `python3 -m venv` fails with *"ensurepip is not available"*
because Debian splits `ensurepip` into the `python3-venv` package, which needs
root to install. The workaround in `setup-toolchain.sh` is:

```bash
python3 -m venv --without-pip .venv
curl -sSL -o /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py
.venv/bin/python /tmp/get-pip.py
```

Torch is installed from the CPU-only index
(`--index-url https://download.pytorch.org/whl/cpu`). The model is ~5k
parameters and trains in minutes on CPU; pulling the default wheel would drag
in ~2 GB of CUDA runtime for nothing.

### CMSIS submodules

```bash
git submodule update --init --depth 1 --recursive
```

`third_party/CMSIS-NN` and `third_party/CMSIS-DSP` are submodules rather than
vendored copies: they are ARM's upstream repositories, they are large, and
pinning them by commit is both smaller in git and more honest about provenance.
The host build does **not** need them — it uses the portable reference kernels
in `firmware/src/nn/` — so unit tests and the simulator work on a fresh clone
with no submodules fetched.

## Building

```bash
# Host: unit tests + simulator
make test
make sim

# Target: STM32F446RE
make firmware              # -> build/arm/cardia.elf, .bin, .map
make size                  # flash/RAM breakdown from the map file
```

## Flashing

The Nucleo-F446RE exposes an ST-LINK/V2-1 mass-storage bootloader, so the
lowest-friction path needs no tools at all:

```bash
cp build/arm/cardia.bin /media/$USER/NODE_F446RE/
```

With `openocd` available:

```bash
openocd -f board/st_nucleo_f4.cfg -c "program build/arm/cardia.elf verify reset exit"
```

## Verified versions

| Tool | Version | Verified |
|---|---|---|
| `arm-none-eabi-gcc` | 14.2.1 20241119 (Arm GNU 14.2.Rel1) | yes |
| `gcc` (host) | system | yes |
| CMake | 4.4.0 (pip wheel) | yes |
| Ninja | 1.13.0 (pip wheel) | yes |
| Python | 3.14.4 | yes |
| numpy / scipy / wfdb | 2.5.1 / 1.18.0 / 4.3.1 | yes |
| PyTorch | CPU wheel | see `docs/RESULTS.md` |
