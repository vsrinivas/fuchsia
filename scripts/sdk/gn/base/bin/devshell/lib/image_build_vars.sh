#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Script to provide necessary variables for fx emu to run out-of-tree
# This file must be located in `dirname emu`/lib so that emu can source it.
SCRIPT_SRC_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )/../.." >/dev/null 2>&1 && pwd)"
source "${SCRIPT_SRC_DIR}"/fuchsia-common.sh || exit $?
source "${SCRIPT_SRC_DIR}"/fx-image-common.sh || exit $?
