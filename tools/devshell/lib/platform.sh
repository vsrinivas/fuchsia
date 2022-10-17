# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

case "$(uname -s)" in
  Linux)
    readonly HOST_OS="linux"
    ;;
  Darwin)
    readonly HOST_OS="mac"
    ;;
  *)
    echo >&2 "Unknown operating system: $(uname -s)."
    exit 1
    ;;
esac

case "$(uname -m)" in
  x86_64)
    readonly REAL_HOST_CPU="x64"
    ;;
  aarch64 | arm64)
    readonly REAL_HOST_CPU="arm64"
    ;;
  *)
    echo >&2 "Unknown architecture: $(uname -m)."
    exit 1
    ;;
esac

if [[ "$HOST_OS" == "mac" ]]; then
  # Redirect mac-arm64 to mac-x64 binaries until prebuilt arm64 binaries are
  # available for all packages.
  # TODO(fxbug.dev/97767): Provide arm64 Mac versions of all packages.
  readonly HOST_CPU="x64"
else
  readonly HOST_CPU="$REAL_HOST_CPU"
fi

readonly HOST_PLATFORM="${HOST_OS}-${HOST_CPU}"
# Packages that *do* have a mac-arm64 version available can use
# REAL_HOST_PLATFORM instead of HOST_PLATFORM.
readonly REAL_HOST_PLATFORM="${HOST_OS}-${REAL_HOST_CPU}"

readonly PREBUILT_3P_DIR="${FUCHSIA_DIR}/prebuilt/third_party"
readonly PREBUILT_TOOLS_DIR="${FUCHSIA_DIR}/prebuilt/tools"

readonly PREBUILT_AEMU_DIR="${PREBUILT_3P_DIR}/android/aemu/release/${HOST_PLATFORM}"
readonly PREBUILT_BUILDIFIER="${PREBUILT_3P_DIR}/buildifier/${HOST_PLATFORM}/buildifier"
readonly PREBUILT_BUILDOZER="${PREBUILT_3P_DIR}/buildozer/${HOST_PLATFORM}/buildozer"
readonly PREBUILT_BINUTILS_DIR="${PREBUILT_3P_DIR}/binutils-gdb/${HOST_PLATFORM}"
readonly PREBUILT_CLANG_DIR="${PREBUILT_3P_DIR}/clang/${REAL_HOST_PLATFORM}"
readonly PREBUILT_CMAKE_DIR="${PREBUILT_3P_DIR}/cmake/${HOST_PLATFORM}"
readonly PREBUILT_DART_DIR="${PREBUILT_3P_DIR}/dart/${HOST_PLATFORM}"
readonly PREBUILT_EDK2_DIR="${PREBUILT_3P_DIR}/edk2"
readonly PREBUILT_GCC_DIR="${PREBUILT_3P_DIR}/gcc/${HOST_PLATFORM}"
readonly PREBUILT_GN="${PREBUILT_3P_DIR}/gn/${REAL_HOST_PLATFORM}/gn"
readonly PREBUILT_GO_DIR="${PREBUILT_3P_DIR}/go/${HOST_PLATFORM}"
readonly PREBUILT_GOMA_DIR="${PREBUILT_3P_DIR}/goma/${HOST_PLATFORM}"
readonly PREBUILT_GRPCWEBPROXY_DIR="${PREBUILT_3P_DIR}/grpcwebproxy/${HOST_PLATFORM}"
readonly PREBUILT_NINJA="${PREBUILT_3P_DIR}/ninja/${HOST_PLATFORM}/ninja"
readonly PREBUILT_PYTHON3_DIR="${PREBUILT_3P_DIR}/python3/${HOST_PLATFORM}"
readonly PREBUILT_OVMF_DIR="${PREBUILT_EDK2_DIR}/x64"
readonly PREBUILT_QEMU_DIR="${PREBUILT_3P_DIR}/qemu/${HOST_PLATFORM}"
readonly PREBUILT_RUST_DIR="${PREBUILT_3P_DIR}/rust/${HOST_PLATFORM}"
readonly PREBUILT_RUST_BINDGEN_DIR="${PREBUILT_3P_DIR}/rust_bindgen/${HOST_PLATFORM}"
readonly PREBUILT_RUST_CARGO_OUTDATED_DIR="${PREBUILT_3P_DIR}/rust_cargo_outdated/${HOST_PLATFORM}"
readonly PREBUILT_CGPT_DIR="${PREBUILT_TOOLS_DIR}/cgpt/${HOST_PLATFORM}"
readonly PREBUILT_FUTILITY_DIR="${PREBUILT_TOOLS_DIR}/futility/${HOST_PLATFORM}"
readonly PREBUILT_VDL_DIR="${FUCHSIA_DIR}/prebuilt/vdl"
readonly PREBUILT_RECLIENT_DIR="${FUCHSIA_DIR}/prebuilt/proprietary/third_party/reclient/${HOST_PLATFORM}"
