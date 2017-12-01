#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# NOTE: installing cargo-vendor manually is currently necessary as cargo-vendor
# cannot be built from sources in the Fuchsia tree.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname $(dirname "${SCRIPT_DIR}"))"

if [[ "$(uname -s)" = "Darwin" ]]; then
  readonly TRIPLE="x86_64-apple-darwin"
else
  readonly TRIPLE="x86_64-unknown-linux-gnu"
fi
readonly RUST_BASE="$ROOT_DIR/buildtools/rust/rust-$TRIPLE"
readonly CARGO="$RUST_BASE/bin/cargo"

export PATH="$PATH:$ROOT_DIR/buildtools/cmake/bin"
export RUSTC="$RUST_BASE/bin/rustc"

$CARGO install cargo-vendor
