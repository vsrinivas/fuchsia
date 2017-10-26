# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  devshell_lib_dir=${${(%):-%x}:a:h}
else
  devshell_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

export FUCHSIA_DIR="$(dirname $(dirname $(dirname "${devshell_lib_dir}")))"
export FUCHSIA_OUT_DIR="${FUCHSIA_OUT_DIR:-${FUCHSIA_DIR}/out}"
export FUCHSIA_CONFIG="${FUCHSIA_CONFIG:-${FUCHSIA_DIR}/.config}"

function fx_config_read {
  if [[ ! -f "${FUCHSIA_CONFIG}" ]]; then
    echo >& 2 "error: Cannot read config from ${FUCHSIA_CONFIG}. Did you run \"fx set\"?"
    exit 1
  fi

  source "${FUCHSIA_CONFIG}"

  export FUCHSIA_BUILD_DIR="${FUCHSIA_BUILD_DIR}"
  export FUCHSIA_VARIANT="${FUCHSIA_VARIANT}"
  export FUCHSIA_ARCH="${FUCHSIA_ARCH}"
  export ZIRCON_PROJECT="${ZIRCON_PROJECT}"

  export ZIRCON_BUILD_DIR="${ZIRCON_BUILD_DIR:-${FUCHSIA_OUT_DIR}/build-zircon/build-${ZIRCON_PROJECT}}"
}
