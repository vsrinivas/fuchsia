#!/usr/bin/env bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_ROOT="${SCRIPT_ROOT}/../.."
readonly GARNET_ROOT="${FUCHSIA_ROOT}/garnet"
readonly BUILDTOOLS_DIR="${FUCHSIA_ROOT}/buildtools"

INTERNAL_ACCESS=false
if [[ "$(${BUILDTOOLS_DIR}/cipd ls fuchsia_internal)" != "No matching packages." ]]; then
  ${BUILDTOOLS_DIR}/cipd ensure -ensure-file "${SCRIPT_ROOT}/cipd.ensure" -root ${GARNET_ROOT} -log-level warning
  INTERNAL_ACCESS=true
fi
echo "internal_access = ${INTERNAL_ACCESS}" > "${SCRIPT_ROOT}/cipd.gni"
