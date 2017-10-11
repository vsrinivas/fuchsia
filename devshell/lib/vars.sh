#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  readonly FUCHSIA_SCRIPTS_DIR=${${(%):-%x}:a:h}
else
  readonly FUCHSIA_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

export FUCHSIA_DIR="$(dirname "${FUCHSIA_SCRIPTS_DIR}")"
export FUCHSIA_OUT_DIR="${FUCHSIA_DIR}/out"
export FUCHSIA_BUILD_DIR="${FUCHSIA_OUT_DIR}/${FUCHSIA_VARIANT:-debug}-${FUCHSIA_GEN_TARGET:-x86-64}"
