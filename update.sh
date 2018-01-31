#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

source "${SCRIPT_ROOT}/vars.sh"

# TODO(phosek): remove this after we finish migration to CIPD
rm -rf "${BUILDTOOLS_DIR}/linux64" "${BUILDTOOLS_DIR}/mac" "${BUILDTOOLS_DIR}/win"
if [[ ! -d "${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}/.cipd" ]]; then
  rm -rf "${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}"
fi

${SCRIPT_ROOT}/cipd ensure -ensure-file "${SCRIPT_ROOT}/fuchsia.ensure" -root "${BUILDTOOLS_DIR}/${BUILDTOOLS_PLATFORM}" -log-level warning
