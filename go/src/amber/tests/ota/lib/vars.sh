#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

export TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -f "${FUCHSIA_BUILD_DIR}/fx.config" ]]; then
  source "${FUCHSIA_BUILD_DIR}/fx.config"
fi

# Paths are relative to FUCHSIA_DIR unless they're absolute paths.
if [[ "${FUCHSIA_BUILD_DIR:0:1}" != "/" ]]; then
  FUCHSIA_BUILD_DIR="${FUCHSIA_DIR}/${FUCHSIA_BUILD_DIR}"
fi

export FUCHSIA_BUILD_DIR FUCHSIA_ARCH
export FUCHSIA_OUT_DIR="${FUCHSIA_OUT_DIR:-${FUCHSIA_DIR}/out}"
export ZIRCON_TOOLS_DIR="${FUCHSIA_OUT_DIR}/build-zircon/tools"
export TEST_LIB_DIR="${TEST_DIR}/lib"
