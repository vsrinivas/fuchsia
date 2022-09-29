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

function ensure-symbol-index-registered {
  FUCHSIA_ANALYTICS_DISABLED=1 fx-command-run \
    ffx debug symbol-index add "${FUCHSIA_BUILD_DIR}/.symbol-index.json" || return $?
}
