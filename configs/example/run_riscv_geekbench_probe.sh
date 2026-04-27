#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
GEM5_ROOT=$(cd -- "${SCRIPT_DIR}/../.." && pwd)

GEM5_BIN=${GEM5_BIN:-"${GEM5_ROOT}/build/RISCV/gem5.opt"}
OUTDIR=${OUTDIR:-"${HOME}/Developer/geek_results"}
GEEKBENCH_DIR=${GEEKBENCH_DIR:-"${HOME}/Developer/Geekbench-6.7.0-LinuxRISCVPreview"}
GEEKBENCH_BINARY=${GEEKBENCH_BINARY:-geekbench6}
RISCV_SYSROOT=${RISCV_SYSROOT:-/usr/riscv64-linux-gnu}
GEEKBENCH_ARGS=${GEEKBENCH_ARGS:-}
HOST_ARCH=${HOST_ARCH:-"$(uname -m)"}

LOADER="${RISCV_SYSROOT}/lib/ld-linux-riscv64-lp64d.so.1"
USE_NATIVE_RISCV=0

if [[ "${HOST_ARCH}" == riscv64* ]]; then
    USE_NATIVE_RISCV=1
fi

if [[ ! -x "${GEM5_BIN}" ]]; then
    echo "gem5 binary not found: ${GEM5_BIN}" >&2
    echo "Build it with: scons build/RISCV/gem5.opt -j\$(nproc)" >&2
    exit 1
fi

if [[ ! -x "${GEEKBENCH_DIR}/${GEEKBENCH_BINARY}" ]]; then
    echo "Geekbench binary not found: ${GEEKBENCH_DIR}/${GEEKBENCH_BINARY}" >&2
    exit 1
fi

if [[ "${USE_NATIVE_RISCV}" -eq 0 && ! -e "${LOADER}" ]]; then
    echo "RISC-V dynamic loader not found: ${LOADER}" >&2
    echo "Install the runtime sysroot with:" >&2
    echo "  sudo apt install --no-install-recommends libc6-riscv64-cross libgcc-s1-riscv64-cross" >&2
    exit 1
fi

mkdir -p "${OUTDIR}"

cmd=(
    "${GEM5_BIN}"
    -d "${OUTDIR}"
    "${GEM5_ROOT}/configs/example/riscv_geekbench_probe.py"
    --geekbench-dir "${GEEKBENCH_DIR}"
    --geekbench-binary "${GEEKBENCH_BINARY}"
)

if [[ "${USE_NATIVE_RISCV}" -eq 0 ]]; then
    cmd+=(
        --interp-dir "${RISCV_SYSROOT}"
        --redirects "/lib=${RISCV_SYSROOT}/lib"
    )
fi

if [[ -n "${GEEKBENCH_ARGS}" ]]; then
    cmd+=("--geekbench-args=${GEEKBENCH_ARGS}")
fi

cmd+=("$@")

exec "${cmd[@]}"
