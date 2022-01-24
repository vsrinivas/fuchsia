#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

### Runs the gerrit-submit script inside the fx environment

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&2 && pwd)"/../../lib/vars.sh || exit $?
fx-config-read

"${PREBUILT_PYTHON3_DIR}/bin/python3.8" "${FUCHSIA_DIR}/tools/devshell/contrib/gerrit-submit-lib/submit.py" "${@:1}"
