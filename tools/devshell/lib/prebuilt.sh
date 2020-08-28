# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

case "$(uname -s)-$(uname -m)" in
  Darwin-x86_64)
    readonly HOST_PLATFORM="mac-x64"
    ;;
  Linux-x86_64)
    readonly HOST_PLATFORM="linux-x64"
    ;;
  Linux-aarch64)
    readonly HOST_PLATFORM="linux-arm64"
    ;;
  *)
    echo >&2 "Unknown operating system."
    exit 1
    ;;
esac

readonly PREBUILT_3P_DIR="${FUCHSIA_DIR}/prebuilt/third_party"
readonly PREBUILT_TOOLS_DIR="${FUCHSIA_DIR}/prebuilt/tools"

readonly PREBUILT_AEMU_DIR="${PREBUILT_3P_DIR}/aemu/${HOST_PLATFORM}"
readonly PREBUILT_CLANG_DIR="${PREBUILT_3P_DIR}/clang/${HOST_PLATFORM}"
readonly PREBUILT_CMAKE_DIR="${PREBUILT_3P_DIR}/cmake/${HOST_PLATFORM}"
readonly PREBUILT_DART_DIR="${PREBUILT_3P_DIR}/dart/${HOST_PLATFORM}"
readonly PREBUILT_GN="${PREBUILT_3P_DIR}/gn/${HOST_PLATFORM}/gn"
readonly PREBUILT_GO_DIR="${PREBUILT_3P_DIR}/go/${HOST_PLATFORM}"
readonly PREBUILT_GOMA_DIR="${PREBUILT_3P_DIR}/goma/${HOST_PLATFORM}"
readonly PREBUILT_GRPCWEBPROXY_DIR="${PREBUILT_3P_DIR}/grpcwebproxy/${HOST_PLATFORM}"
readonly PREBUILT_NINJA="${PREBUILT_3P_DIR}/ninja/${HOST_PLATFORM}/ninja"
readonly PREBUILT_PYTHON3_DIR="${PREBUILT_3P_DIR}/python3/${HOST_PLATFORM}"
readonly PREBUILT_OVMF_DIR="${PREBUILT_3P_DIR}/ovmf/${HOST_PLATFORM}"
readonly PREBUILT_QEMU_DIR="${PREBUILT_3P_DIR}/qemu/${HOST_PLATFORM}"
readonly PREBUILT_RUST_DIR="${PREBUILT_3P_DIR}/rust/${HOST_PLATFORM}"
readonly PREBUILT_RUST_TOOLS_DIR="${PREBUILT_3P_DIR}/rust_tools/${HOST_PLATFORM}"
readonly PREBUILT_CGPT_DIR="${PREBUILT_TOOLS_DIR}/cgpt/${HOST_PLATFORM}"
readonly PREBUILT_FUTILITY_DIR="${PREBUILT_TOOLS_DIR}/futility/${HOST_PLATFORM}"
