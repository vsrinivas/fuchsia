#!/usr/bin/env bash

# Copyright 2022 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ZIRCON_DIR="${DIR}/../../../../.."
SCRIPTS_DIR="${ZIRCON_DIR}/scripts"

"${SCRIPTS_DIR}/package-image.sh" -b pinecrest \
    -d "$ZIRCON_DIR/kernel/target/arm64/dtb/dummy-device-tree.dtb" -D mkbootimg \
    -l -M 0x02000000 -K 0x00000000 -B "$(pwd)" $@
