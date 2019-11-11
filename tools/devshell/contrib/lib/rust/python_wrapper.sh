#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### Runs a Rust helper script inside the fx environment

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&2 && pwd)"/../lib/vars.sh || exit $?
fx-config-read

python "${FUCHSIA_DIR}/tools/devshell/contrib/lib/rust/$(basename $0).py" "${@:1}"
