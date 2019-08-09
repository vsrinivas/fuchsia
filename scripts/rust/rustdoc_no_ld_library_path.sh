#!/usr/bin/env bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is a thin wrapper for rustdoc that unsets LD_LIBRARY_PATH
# to avoid the incorrect env provided by `cargo doc`.
#
# TODO(cramertj) remove pending fix to our builds of cargo doc to prevent this

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
readonly ROOT_DIR="$(dirname $(dirname "${SCRIPT_DIR}"))"

if [[ "$(uname -s)" = "Darwin" ]]; then
  readonly PLATFORM="mac-x64"
else
  readonly PLATFORM="linux-x64"
fi

unset LD_LIBRARY_PATH
$ROOT_DIR/prebuilt/third_party/rust/$PLATFORM/bin/rustdoc --cap-lints=allow "$@"
