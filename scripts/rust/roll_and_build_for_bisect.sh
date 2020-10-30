#!/bin/bash -e
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Called by `fx bisect-rustc`. See that command's `--help` for details.

FUCHSIA_DIR="$1"
shift
PLATFORM="$1"
shift
REV=$(git rev-parse BISECT_HEAD)

pushd "$FUCHSIA_DIR"
# If roll-compiler fails, exit 125 to tell git bisect we can't test this revision.
echo "rolling compiler for ${PLATFORM}"
fx roll-compiler --package rust --platforms "$PLATFORM" git_revision:$REV || exit 125

echo 'fetching CIPD packages'
set -xe
jiri fetch-packages -local-manifest
fx build "$@"
