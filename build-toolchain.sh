#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# N.B. This must be an absolute path.
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

readonly HOST_ARCH=$(uname -m)
readonly HOST_OS=$(uname | sed 'y/LINUXDARWIN/linuxdarwin/')
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"

if [[ "x${HOST_OS}" == "xlinux" ]]; then
  readonly DEFAULT_JOBS=$(grep ^processor /proc/cpuinfo | wc -l)
  readonly CMAKE_HOST_TOOLS="\
    -DCMAKE_MAKE_PROGRAM=${ROOT_DIR}/buildtools/ninja \
    -DCMAKE_C_COMPILER=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/clang \
    -DCMAKE_CXX_COMPILER=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/clang++ \
    -DCMAKE_AR=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/llvm-ar \
    -DCMAKE_NM=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/llvm-nm \
    -DCMAKE_RANLIB=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/llvm-ranlib \
    -DCMAKE_OBJDUMP=${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/bin/llvm-objdump \
    -DCMAKE_OBJCOPY=false \
    -DCMAKE_STRIP=false"
  readonly LLVM_CONFIG_OPTS="-DLLVM_ENABLE_LLD=ON"
elif [[ "x${HOST_OS}" == "xdarwin" ]]; then
  readonly DEFAULT_JOBS=$(sysctl -n hw.ncpu)
  readonly CMAKE_HOST_TOOLS="\
    -DCMAKE_MAKE_PROGRAM=${ROOT_DIR}/buildtools/ninja"
else
  echo "Unsupported system: ${HOST_OS}" 1>&2
  exit 1
fi

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  printf >&2 '%s: [-c] [-o outdir] [-d destdir] [-j jobs]\n' "$0"
  echo >&2 "-c:         clean the build directories first"
  echo >&2 "-o outdir:  build the tools here"
  echo >&2 "-d destdir: install the tools here"
  echo >&2 "-j jobs:    passed to make/ninja"
  exit 1
}

build() {
  local outdir="$1" destdir="$2" clean="$3" jobs="$4"
  local toolchain="${destdir}/clang+llvm-${HOST_TRIPLE}"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${outdir}/build-binutils-gdb-${HOST_TRIPLE}" "${outdir}/build-clang+llvm-${HOST_TRIPLE}" "${outdir}/build-compiler-rt-aarch64+x86_64"
  fi

  rm -rf -- "${toolchain}" && mkdir -p -- "${toolchain}"

  mkdir -p -- "${outdir}/build-binutils-gdb-${HOST_TRIPLE}"
  pushd "${outdir}/build-binutils-gdb-${HOST_TRIPLE}"
  [[ -f "${outdir}/build-binutils-gdb-${HOST_TRIPLE}/Makefile" ]] || ${ROOT_DIR}/third_party/binutils-gdb/configure \
    --prefix='' \
    --program-prefix='' \
    --enable-targets=arm-elf,aarch64-elf,x86_64-elf \
    --enable-deterministic-archives \
    --disable-werror \
    --disable-nls
  make -j "${jobs}" all-binutils
  mkdir -p -- "${toolchain}/bin"
  cp -- "${outdir}/build-binutils-gdb-${HOST_TRIPLE}/binutils/objcopy" "${toolchain}/bin/objcopy"
  cp -- "${outdir}/build-binutils-gdb-${HOST_TRIPLE}/binutils/strip-new" "${toolchain}/bin/strip"
  popd

  mkdir -p -- "${outdir}/build-clang+llvm-${HOST_TRIPLE}"
  pushd "${outdir}/build-clang+llvm-${HOST_TRIPLE}"
  [[ -f "${outdir}/build-clang+llvm-${HOST_TRIPLE}/build.ninja" ]] || ${ROOT_DIR}/buildtools/cmake/bin/cmake -GNinja \
    ${CMAKE_HOST_TOOLS:-} \
    ${LLVM_CONFIG_OPTS:-} \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX='' \
    -DLLVM_APPEND_VC_REV=ON \
    -DLLVM_ENABLE_LIBCXX=ON \
    -DLLVM_ENABLE_LTO=ON \
    -DLLVM_TARGETS_TO_BUILD='ARM;AArch64;X86' \
    -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
    -DLLVM_TOOLCHAIN_TOOLS='llvm-ar;llvm-cxxfilt;llvm-ranlib;llvm-dwarfdump;llvm-objdump;llvm-readobj;llvm-nm;llvm-size;llvm-symbolizer' \
    ${ROOT_DIR}/third_party/llvm
  env LD_LIBRARY_PATH="${ROOT_DIR}/buildtools/toolchain/clang+llvm-${HOST_TRIPLE}/lib" DESTDIR="${toolchain}" ${ROOT_DIR}/buildtools/ninja -j "${jobs}" install
  popd

  mkdir -p -- "${outdir}/build-compiler-rt-aarch64+x86_64"
  pushd "${outdir}/build-compiler-rt-aarch64+x86_64"
  [[ -f "${outdir}/build-compiler-rt-aarch64+x86_64/build.ninja" ]] || CFLAGS="-fPIC -isystem ${ROOT_DIR}/magenta/third_party/ulib/musl/include" ${ROOT_DIR}/buildtools/cmake/bin/cmake -GNinja \
    -DCMAKE_MAKE_PROGRAM=${ROOT_DIR}/buildtools/ninja \
    -DCMAKE_C_COMPILER=${toolchain}/bin/clang \
    -DCMAKE_CXX_COMPILER=${toolchain}/bin/clang++ \
    -DCMAKE_AR=${toolchain}/bin/llvm-ar \
    -DCMAKE_NM=${toolchain}/bin/llvm-nm \
    -DCMAKE_RANLIB=${toolchain}/bin/llvm-ranlib \
    -DCMAKE_OBJDUMP=${toolchain}/llvm-objdump \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX= \
    -DCMAKE_TOOLCHAIN_FILE=${ROOT_DIR}/third_party/llvm/cmake/platforms/Fuchsia.cmake \
    -DLLVM_CONFIG_PATH=${outdir}/build-clang+llvm-${HOST_TRIPLE}/bin/llvm-config \
    ${ROOT_DIR}/third_party/llvm/runtimes/compiler-rt/lib/builtins
  env DESTDIR="${toolchain}" ${ROOT_DIR}/buildtools/ninja -j "${jobs}" install
  popd

  local stamp="$(LC_ALL=POSIX cat $(find "${toolchain}" -type f | sort) | shasum -a1  | awk '{print $1}')"
  echo "${stamp}" > "${toolchain}/.stamp"
}

declare CLEAN="${CLEAN:-false}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare DESTDIR="${DESTDIR:-${OUTDIR}/toolchain}"
declare JOBS="${DEFAULT_JOBS}"

while getopts "cd:j:o:" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    d) DESTDIR="${OPTARG}" ;;
    j) JOBS="${OPTARG}" ;;
    o) OUTDIR="${OPTARG}" ;;
    *) usage;;
  esac
done

absolute_path() {
  local -r path="$1"
  case "$path" in
    /*) echo "$path" ;;
    *) echo "$(pwd)/$path" ;;
  esac
}

# These must be absolute paths.
OUTDIR=$(absolute_path "${OUTDIR}")
DESTDIR=$(absolute_path "${DESTDIR}")

readonly CLEAN OUTDIR DESTDIR JOBS

build "${OUTDIR}" "${DESTDIR}" "${CLEAN}" "${JOBS}"
