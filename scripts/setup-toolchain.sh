#!/usr/bin/env bash
# Cardia toolchain bootstrap.
#
# Sets up everything needed to build both tracks:
#   * ARM GNU bare-metal toolchain (arm-none-eabi-gcc) for the STM32F446RE build
#   * CMake + Ninja
#   * a project-local Python virtualenv for the ML track
#
# Written for a machine with NO root access (no `sudo apt install`), which is
# the constraint this project was actually developed under. Everything lands in
# $HOME/.local or in the repo, and nothing touches system packages.
#
# Usage:  ./scripts/setup-toolchain.sh          # everything
#         ./scripts/setup-toolchain.sh arm      # just the ARM toolchain
#         ./scripts/setup-toolchain.sh python   # just the venv
#         source ./scripts/setup-toolchain.sh env   # only export PATH

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARM_VERSION="14.2.rel1"
ARM_DIR="${HOME}/.local/arm"
ARM_ROOT="${ARM_DIR}/arm-gnu-toolchain-${ARM_VERSION}-x86_64-arm-none-eabi"
ARM_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_VERSION}/binrel/arm-gnu-toolchain-${ARM_VERSION}-x86_64-arm-none-eabi.tar.xz"
# Fallback if developer.arm.com is unreachable or rate-limits.
XPACK_VERSION="14.2.1-1.1"
XPACK_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${XPACK_VERSION}/xpack-arm-none-eabi-gcc-${XPACK_VERSION}-linux-x64.tar.gz"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }

install_arm() {
  if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    log "arm-none-eabi-gcc already on PATH: $(arm-none-eabi-gcc --version | head -1)"
    return 0
  fi
  if [ -x "${ARM_ROOT}/bin/arm-none-eabi-gcc" ]; then
    log "ARM toolchain already extracted at ${ARM_ROOT}"
    return 0
  fi

  mkdir -p "${ARM_DIR}"
  log "downloading ARM GNU toolchain ${ARM_VERSION} (~150 MB)"
  if curl -fsSL -o "${ARM_DIR}/arm-gnu.tar.xz" "${ARM_URL}"; then
    log "extracting"
    tar -C "${ARM_DIR}" -xf "${ARM_DIR}/arm-gnu.tar.xz"
    rm -f "${ARM_DIR}/arm-gnu.tar.xz"
  else
    warn "developer.arm.com download failed, trying the xPack mirror"
    curl -fsSL -o "${ARM_DIR}/xpack.tar.gz" "${XPACK_URL}"
    tar -C "${ARM_DIR}" -xf "${ARM_DIR}/xpack.tar.gz"
    rm -f "${ARM_DIR}/xpack.tar.gz"
    ln -sfn "${ARM_DIR}/xpack-arm-none-eabi-gcc-${XPACK_VERSION}" "${ARM_ROOT}"
  fi

  "${ARM_ROOT}/bin/arm-none-eabi-gcc" --version | head -1
}

install_build_tools() {
  local py="${REPO_ROOT}/.venv/bin/python"
  [ -x "${py}" ] || py="$(command -v python3)"
  if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    log "cmake and ninja already available"
    return 0
  fi
  log "installing cmake + ninja via pip (no root needed)"
  "${py}" -m pip install --quiet cmake ninja
}

install_python() {
  log "creating project virtualenv at ${REPO_ROOT}/.venv"
  # Some distributions ship python3 without ensurepip (Debian splits it into
  # python3-venv, which needs root). Create the venv bare and bootstrap pip by
  # hand so this works without any system package installs.
  if ! python3 -m venv "${REPO_ROOT}/.venv" 2>/dev/null; then
    warn "ensurepip unavailable; creating venv without pip and bootstrapping"
    rm -rf "${REPO_ROOT}/.venv"
    python3 -m venv --without-pip "${REPO_ROOT}/.venv"
    curl -fsSL -o /tmp/get-pip.py https://bootstrap.pypa.io/get-pip.py
    "${REPO_ROOT}/.venv/bin/python" /tmp/get-pip.py
  fi
  log "installing Python requirements"
  "${REPO_ROOT}/.venv/bin/pip" install --quiet -r "${REPO_ROOT}/ml/requirements.txt"
  "${REPO_ROOT}/.venv/bin/python" - <<'PY'
import numpy, scipy, torch, wfdb
print(f"numpy {numpy.__version__}  scipy {scipy.__version__}  "
      f"torch {torch.__version__}  wfdb {wfdb.__version__}")
PY
}

export_env() {
  if [ -d "${ARM_ROOT}/bin" ]; then
    export PATH="${ARM_ROOT}/bin:${PATH}"
  fi
  export PATH="${REPO_ROOT}/.venv/bin:${PATH}"
}

fetch_submodules() {
  if [ -f "${REPO_ROOT}/.gitmodules" ]; then
    log "fetching CMSIS submodules"
    git -C "${REPO_ROOT}" submodule update --init --depth 1 --recursive || \
      warn "submodule fetch failed; the target build needs third_party/CMSIS-NN"
  fi
}

main() {
  case "${1:-all}" in
    arm)    install_arm ;;
    python) install_python ;;
    tools)  install_build_tools ;;
    env)    export_env ;;
    all)
      install_python
      install_build_tools
      install_arm
      fetch_submodules
      export_env
      log "done. add this to your shell profile:"
      echo "    export PATH=\"${ARM_ROOT}/bin:\$PATH\""
      ;;
    *) warn "unknown target '$1'"; exit 1 ;;
  esac
}

# Allow `source scripts/setup-toolchain.sh env` to only mutate PATH.
if [ "${BASH_SOURCE[0]}" != "${0}" ]; then
  export_env
else
  main "$@"
fi
