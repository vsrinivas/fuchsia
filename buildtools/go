#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is only used in building the Fuchsia Go toolchain.
# See //third_party/go/BUILD.gn.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

# Setting GOROOT is a workaround for https://golang.org/issue/18678.
# Basically, early Go toolchains were not self-relocating.
# Remove this when we're using Go 1.9.
source "$SCRIPT_ROOT/vars.sh"
export GOROOT="$BUILDTOOLS_GO_DIR"

readonly TOOL_NAME="go/bin/go"
source "${SCRIPT_ROOT}/exec_tool.sh"
