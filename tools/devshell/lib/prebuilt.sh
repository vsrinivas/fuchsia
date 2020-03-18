# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

case "$(uname -s)" in
  Darwin)
    readonly HOST_PLATFORM="mac-x64"
    ;;
  Linux)
    readonly HOST_PLATFORM="linux-x64"
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
readonly PREBUILT_QEMU_DIR="${PREBUILT_3P_DIR}/qemu/${HOST_PLATFORM}"
readonly PREBUILT_RUST_DIR="${PREBUILT_3P_DIR}/rust/${HOST_PLATFORM}"
