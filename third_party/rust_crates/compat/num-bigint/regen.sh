#!/usr/bin/env bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

cd "$(dirname "$0")"

source ../../../../tools/devshell/lib/vars.sh

TEMP_DIR=$(mktemp -d /tmp/fuchsia-num-bigint-regen.XXX)
readonly TEMP_DIR
trap 'rm -r ${TEMP_DIR}' EXIT

cp -R ../../vendor/num-bigint "$TEMP_DIR"

pushd "$TEMP_DIR"/num-bigint
"${PREBUILT_RUST_DIR}"/bin/cargo build
popd

find "$TEMP_DIR" -name radix_bases.rs -exec cp '{}' . \;
