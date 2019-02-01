#!/usr/bin/env bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_ROOT="${SCRIPT_ROOT}/../.."
readonly GARNET_ROOT="${FUCHSIA_ROOT}/garnet"
readonly BUILDTOOLS_DIR="${FUCHSIA_ROOT}/buildtools"
readonly CIPD="${BUILDTOOLS_DIR}/cipd"

INTERNAL_ACCESS=false
if [[ "$(${CIPD} ls fuchsia_internal)" != "No matching packages." ]]; then
  INTERNAL_ACCESS=true
fi
echo "internal_access = ${INTERNAL_ACCESS}" > "${SCRIPT_ROOT}/cipd.gni"

declare -a ENSURE_FILES=("${SCRIPT_ROOT}/cipd.ensure")
if $INTERNAL_ACCESS; then
  ENSURE_FILES+=("${SCRIPT_ROOT}/cipd_internal.ensure")
fi

(sed '/^\$/!d' "${ENSURE_FILES[@]}" && sed '/^\$/d' "${ENSURE_FILES[@]}") |
  ${CIPD} ensure -ensure-file - -root ${GARNET_ROOT} -log-level warning
