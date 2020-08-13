# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This library is used by:
# * symbol-index
# * debug
# * fidlcat
# * symbolize
#
# This file is not self-contained! ../../lib/vars.sh must be sourced before this file.

function symbol-index {
  local symbol_index="${FUCHSIA_BUILD_DIR}/host-tools/symbol-index"

  if [[ ! -e ${symbol_index} ]]; then
    fx-error "${symbol_index} is not found, make sure you have //bundles:tools (or //tools) in your
    'fx set' command and 'fx build' was executed successfully."
    exit 1
  fi

  "${symbol_index}" "$@"
  return $?
}

function ensure-symbol-index-registered {
  symbol-index add "${FUCHSIA_BUILD_DIR}/.build-id" "${FUCHSIA_BUILD_DIR}" || return $?
  symbol-index add "${ZIRCON_BUILDROOT}/.build-id" "${ZIRCON_BUILDROOT}" || return $?
  symbol-index add "${FUCHSIA_DIR}/prebuilt/.build-id" || return $?
  symbol-index add "${PREBUILT_CLANG_DIR}/lib/debug/.build-id" || return $?
}
