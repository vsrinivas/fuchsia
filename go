#!/usr/bin/env bash
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"

case "$(uname -s)" in
  Darwin)
    readonly CCHOST="darwin"
    readonly HOST_PLATFORM="mac-x64"
    ;;
  Linux)
    readonly CCHOST="linux"
    readonly HOST_PLATFORM="linux-x64"
    ;;
  *)
    echo "Unknown operating system. Cannot run go."
    exit 1
    ;;
esac

# Setting GOROOT is a workaround for https://golang.org/issue/18678.
# Remove this (and switch to exec_tool.sh) when Go 1.9 is released.
export GOROOT="$SCRIPT_ROOT/$HOST_PLATFORM/go"

if [[ "$GOOS" == "fuchsia" ]]; then
	export ZIRCON="$(cd $(dirname ${BASH_SOURCE[0]} )/.. && pwd)/zircon"
	export CC="$GOROOT/misc/fuchsia/gccwrap.sh"
fi

exec "$GOROOT/bin/go" "$@"
