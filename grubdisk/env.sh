#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/../devshell/lib/vars.sh
fx-config-read

export FUCHSIA_GRUB_SCRIPTS_DIR="$FUCHSIA_DIR/scripts/grubdisk"
export FUCHSIA_GRUB_DIR="$FUCHSIA_OUT_DIR/build-grub"
