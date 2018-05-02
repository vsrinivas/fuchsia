#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

source "$SCRIPT_ROOT/vars.sh"

if [[ "$GOOS" != "fuchsia" || "$GOROOT" == "" ]]; then
	# Setting GOROOT is a workaround for https://golang.org/issue/18678.
	# Remove this (and switch to exec_tool.sh) when Go 1.9 is released.
	export GOROOT="$BUILDTOOLS_GO_DIR"
fi

if [[ "$GOOS" == "fuchsia" ]]; then
	export ZIRCON="$(cd $(dirname ${BASH_SOURCE[0]} )/.. && pwd)/zircon"
	export CC="$GOROOT/misc/fuchsia/gccwrap.sh"
fi

exec "$GOROOT/bin/go" "$@"
