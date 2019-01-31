# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/vars.sh || return $?
fx-config-read

source "${FUCHSIA_BUILD_DIR}"/image_paths.sh
source "${FUCHSIA_BUILD_DIR}"/zedboot_image_paths.sh
