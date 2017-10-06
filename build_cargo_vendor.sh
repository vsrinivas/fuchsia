#!/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# NOTE: building cargo-vendor manually is currently necessary as cargo-vendor
# cannot be built from sources in the Fuchsia tree AND cannot be installed via
# "cargo install"...

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

if [[ "$(uname -s)" = "Darwin" ]]; then
  readonly PLATFORM="mac-x64"
else
  readonly PLATFORM="linux-x64"
fi
readonly RUST_BASE="$ROOT_DIR/buildtools/$PLATFORM/rust"
readonly CARGO="$RUST_BASE/bin/cargo"

command -v cmake >/dev/null 2>&1
if [[ "$?" != 0 ]]; then
  echo "cmake not found, aborting"
  exit 1
fi

export RUSTC="$RUST_BASE/bin/rustc"
export CARGO_TARGET_DIR="$ROOT_DIR/out/cargo-vendor"

mkdir -p $CARGO_TARGET_DIR
cd "$ROOT_DIR/third_party/rust-mirrors/cargo-vendor"
$CARGO build
