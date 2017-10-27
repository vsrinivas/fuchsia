#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

devshell_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FUCHSIA_DIR="$(dirname $(dirname $(dirname "${devshell_lib_dir}")))"

if [[ -d "${FUCHSIA_DIR}/.jiri_root/bin" ]]; then
  rm -f "${FUCHSIA_DIR}/.jiri_root/bin/fx"
  ln -s "../../scripts/fx" "${FUCHSIA_DIR}/.jiri_root/bin/fx"
fi
