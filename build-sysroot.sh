#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

readonly HOST_ARCH=$(uname -m)
readonly HOST_OS=$(uname | tr '[:upper:]' '[:lower:]')
readonly HOST_TRIPLE="${HOST_ARCH}-${HOST_OS}"

readonly CMAKE_PROGRAM=${CMAKE_PROGRAM:-${ROOT_DIR}/buildtools/cmake/bin/cmake}

JOBS=`getconf _NPROCESSORS_ONLN` || {
  Cannot get number of processors
  exit 1
}

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

readonly CMAKE_SHARED_FLAGS="\
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
  -DCMAKE_INSTALL_PREFIX='' \
  -DLLVM_PATH=${ROOT_DIR}/third_party/llvm \
  -DLLVM_ENABLE_LIBCXX=ON"

set -eo pipefail; [[ "${TRACE}" ]] && set -x

usage() {
  printf >&2 '%s: [-c] [-r] [-t target] [-o outdir] [-d destdir]\n' "$0" && exit 1
}

build() {
  local target="$1" outdir="$2" destdir="$3" clean="$4" release="$5"
  local sysroot="${destdir}/${target}-fuchsia"
  local magenta_buildroot="${outdir}/build-magenta"

  if [[ "${clean}" = "true" ]]; then
    rm -rf -- "${magenta_buildroot}" "${outdir}/build-libunwind-${target}" "${outdir}/build-libcxxabi-${target}" "${outdir}/build-libcxx-${target}"
  fi

  case "${target}" in
    "x86_64") local magenta_target="magenta-pc-x86-64" ;;
    "aarch64") local magenta_target="magenta-qemu-arm64" ;;
    "*") echo "unknown target '${target}'" 1>&2 && exit 1;;
  esac

  local magenta_sysroot="${magenta_buildroot}/build-${magenta_target}/sysroot"

  if [[ "${release}" = "true" ]]; then
    local cmake_build_type_flags="${CMAKE_SHARED_FLAGS:-} -DCMAKE_BUILD_TYPE=Release"
    local magenta_build_type_flags="DEBUG=0"
  else
    local cmake_build_type_flags="${CMAKE_SHARED_FLAGS:-} -DCMAKE_BUILD_TYPE=Debug"
    local magenta_build_type_flags=""
  fi

  rm -rf -- "${sysroot}" && mkdir -p -- "${sysroot}"

  pushd "${ROOT_DIR}/magenta"
  rm -rf -- "${magenta_sysroot}"
  # build the sysroot for the target architecture
  make -j ${JOBS} ${magenta_build_type_flags:-} BUILDROOT=${magenta_buildroot} ${magenta_target} BUILDDIR_SUFFIX=
  # build host tools
  make -j ${JOBS} BUILDDIR=${outdir}/build-magenta tools
  popd

  cp -r -- \
    "${magenta_sysroot}/include" \
    "${magenta_sysroot}/lib" \
    "${sysroot}"

  if [[ -d "${magenta_sysroot}/debug-info" ]]; then
    cp -r -- \
      "${magenta_sysroot}/debug-info" \
      "${sysroot}"
  fi

  # These are magenta headers for use in building host tools outside
  # of the magenta tree.
  local magenta_host_include="${magenta_buildroot}/build-${magenta_target}/tools/include"
  local out_magenta_host_dir="${outdir}/magenta-host-${target}"
  rm -rf -- "${out_magenta_host_dir}"
  mkdir -p -- "${out_magenta_host_dir}"
  cp -r -- \
   "${magenta_host_include}" \
   "${out_magenta_host_dir}"

  mkdir -p -- "${outdir}/build-libunwind-${target}"
  pushd "${outdir}/build-libunwind-${target}"
  [[ -f "${outdir}/build-libunwind-${target}/build.ninja" ]] || CXXFLAGS="-I${ROOT_DIR}/third_party/llvm/runtimes/libcxx/include" ${CMAKE_PROGRAM} -GNinja \
    ${CMAKE_HOST_TOOLS:-} \
    ${CMAKE_SHARED_FLAGS:-} \
    ${cmake_build_type_flags:-} \
    -DLIBUNWIND_TARGET_TRIPLE="${target}-fuchsia" \
    -DLIBUNWIND_SYSROOT="${sysroot}" \
    -DLIBUNWIND_USE_COMPILER_RT=ON \
    ${ROOT_DIR}/third_party/llvm/runtimes/libunwind
  env DESTDIR="${sysroot}" ${ROOT_DIR}/buildtools/ninja install
  popd

  mkdir -p -- "${outdir}/build-libcxxabi-${target}"
  pushd "${outdir}/build-libcxxabi-${target}"
  [[ -f "${outdir}/build-libcxxabi-${target}/build.ninja" ]] || ${CMAKE_PROGRAM} -GNinja \
    ${CMAKE_HOST_TOOLS:-} \
    ${CMAKE_SHARED_FLAGS:-} \
    ${cmake_build_type_flags:-} \
    -DLIBCXXABI_TARGET_TRIPLE="${target}-fuchsia" \
    -DLIBCXXABI_SYSROOT="${sysroot}" \
    -DLIBCXXABI_LIBCXX_INCLUDES="${ROOT_DIR}/third_party/llvm/runtimes/libcxx/include" \
    -DLIBCXXABI_LIBUNWIND_INCLUDES="${ROOT_DIR}/third_party/llvm/runtimes/libunwind/include" \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_USE_COMPILER_RT=ON \
    ${ROOT_DIR}/third_party/llvm/runtimes/libcxxabi
  env DESTDIR="${sysroot}" ${ROOT_DIR}/buildtools/ninja install
  popd

  mkdir -p -- "${outdir}/build-libcxx-${target}"
  pushd "${outdir}/build-libcxx-${target}"
  [[ -f "${outdir}/build-libcxx-${target}/build.ninja" ]] || ${CMAKE_PROGRAM} -GNinja \
    ${CMAKE_HOST_TOOLS:-} \
    ${CMAKE_SHARED_FLAGS:-} \
    ${cmake_build_type_flags:-} \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXX_CXX_ABI_INCLUDE_PATHS="${ROOT_DIR}/third_party/llvm/runtimes/libcxxabi/include" \
    -DLIBCXX_ABI_VERSION=2 \
    -DLIBCXX_TARGET_TRIPLE="${target}-fuchsia" \
    -DLIBCXX_SYSROOT="${sysroot}" \
    -DLIBCXX_USE_COMPILER_RT=ON \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    ${ROOT_DIR}/third_party/llvm/runtimes/libcxx
  env DESTDIR="${sysroot}" ${ROOT_DIR}/buildtools/ninja install
  popd

  local stamp="$(LC_ALL=POSIX cat $(find "${sysroot}" -type f | sort) | shasum -a1  | awk '{print $1}')"
  echo "${stamp}" > "${sysroot}/.stamp"
}

declare CLEAN="${CLEAN:-false}"
declare TARGET="${TARGET:-x86_64}"
declare OUTDIR="${OUTDIR:-${ROOT_DIR}/out}"
declare DESTDIR="${DESTDIR:-${OUTDIR}/sysroot}"
declare RELEASE="${RELEASE:-false}"

while getopts "cd:t:o:r" opt; do
  case "${opt}" in
    c) CLEAN="true" ;;
    d) DESTDIR="${OPTARG}" ;;
    o) OUTDIR="${OPTARG}" ;;
    r) RELEASE="true" ;;
    t) TARGET="${OPTARG}" ;;
    *) usage;;
  esac
done

readonly CLEAN TARGET OUTDIR DESTDIR RELEASE

build "${TARGET}" "${OUTDIR}" "${DESTDIR}" "${CLEAN}" "${RELEASE}"
