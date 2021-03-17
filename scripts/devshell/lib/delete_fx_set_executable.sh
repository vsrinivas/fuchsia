#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euf -o pipefail

devshell_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
FUCHSIA_DIR="$(dirname $(dirname $(dirname "${devshell_lib_dir}")))"

# Delete the cached fx-set executable (created by //tools/devshell/set) as a
# signal that it needs to be recompiled the first time `fx set` runs after a
# `jiri update`.
if [[ -d "${FUCHSIA_DIR}/.fx" ]]; then
  rm -f "${FUCHSIA_DIR}/.fx/fx-set"
fi
