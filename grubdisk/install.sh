#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh

export GOPATH=$FUCHSIA_DIR/garnet/go
exec go run "$FUCHSIA_GRUB_SCRIPTS_DIR/install.go" "$@"
