#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  BUILDTOOLS_DIR=${${(%):-%x}:a:h}
else
  BUILDTOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

case "$(uname -s)" in
  Darwin)
    BUILDTOOLS_PLATFORM="mac-x64"
    ;;
  Linux)
    BUILDTOOLS_PLATFORM="linux-x64"
    ;;
  *)
    echo "Unknown operating system."
    exit 1
    ;;
esac

BUILDTOOLS_CLANG_DIR="${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}/clang"
BUILDTOOLS_RUST_DIR="${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}/rust"
BUILDTOOLS_GO_DIR="${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}/go"
BUILDTOOLS_QEMU_DIR="${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}/qemu"
