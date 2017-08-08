#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# NOTE: building cargo-vendor manually is currently necessary as cargo-vendor
# cannot be built from sources in the Fuchsia tree AND cannot be installed via
# "cargo install"...

set -e

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname "${SCRIPT_DIR}")"

if [[ "$(uname -s)" = "Darwin" ]]; then
  readonly TRIPLE="x86_64-apple-darwin"
else
  readonly TRIPLE="x86_64-unknown-linux-gnu"
fi
readonly RUST_BASE="$ROOT_DIR/buildtools/rust/rust-$TRIPLE"
readonly CARGO="$RUST_BASE/bin/cargo"

export PATH="$PATH:$ROOT_DIR/buildtools/cmake/bin"
export RUSTC="$RUST_BASE/bin/rustc"
export CARGO_TARGET_DIR="$ROOT_DIR/out/cargo-vendor"

mkdir -p $CARGO_TARGET_DIR
cd "$ROOT_DIR/third_party/rust-crates/manual/cargo-vendor"
$CARGO build
