#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# Builds various tools needed for Rust support.

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly ROOT_DIR="$(dirname $(dirname "${SCRIPT_DIR}"))"

case "$(uname -s)" in
  Darwin)
    readonly PLATFORM="darwin"
    ;;
  Linux)
    readonly PLATFORM="linux"
    ;;
  *)
    echo "Unknown operating system!"
    exit 1
    ;;
esac

readonly RUST_TOOLS="${ROOT_DIR}/rust/magenta-rs/tools"
if [ ! -d "$RUST_TOOLS" ]; then
  echo "Missing Rust tools directory, needs to be added to your Jiri manifest:"
  echo "    jiri import runtimes/rust https://fuchsia.googlesource.com/manifest && jiri update"
  exit 1
fi
cd $RUST_TOOLS
if [ ! -e "clang_wrapper" ]; then
  echo "Building clang_wrapper"
  if [ "$PLATFORM" != "darwin" ]; then
    readonly CLANG="${ROOT_DIR}/buildtools/toolchain/clang+llvm-x86_64-${PLATFORM}/bin/clang++"
  else
    # Just use the Xcode version to avoid include issues.
    readonly CLANG=`which clang++`
    if [ -z "$CLANG" ]; then
      echo "Could not locate clang++, you might need to install Xcode."
      exit 1
    fi
  fi
  $CLANG -O --std=c++11 clang_wrapper.cc -o clang_wrapper
  ln -s clang_wrapper x86-64-unknown-fuchsia-ar
  ln -s clang_wrapper x86-64-unknown-fuchsia-cc
  ln -s clang_wrapper aarch64-unknown-fuchsia-ar
  ln -s clang_wrapper aarch64-unknown-fuchsia-cc
fi
echo -e "\xE2\x9C\x93 clang_wrapper binary"

readonly RUST_DIR="${ROOT_DIR}/third_party/rust"
if [ ! -d "$RUST_DIR" ]; then
  echo "Cloning Rust project"
  git clone https://github.com/rust-lang/rust.git $RUST_DIR
fi
cd $RUST_DIR
readonly RUST_HEAD="373efe8794defedc8ce41e258910560423d0c0b7"
if [ `git rev-parse HEAD` != "$RUST_HEAD" ]; then
  git checkout $RUST_HEAD
fi
echo -e "\xE2\x9C\x93 Rust sources"

readonly RUST_CONFIG="${RUST_DIR}/config.toml"
if [ ! -e "$RUST_CONFIG" ]; then
  cat - <<EOF >$RUST_DIR/config.toml
# Config file for fuchsia target for building Rust.
# See \`src/bootstrap/config.toml.example\` for other settings.

[rust]
# Disable backtrace, as it requires additional lib support.
backtrace = false

[target.x86_64-unknown-fuchsia]
cc = "${RUST_TOOLS}/x86-64-unknown-fuchsia-cc"

[target.aarch64-unknown-fuchsia]
# Path to the clang wrapper
cc = "${RUST_TOOLS}/aarch64-unknown-fuchsia-cc"
EOF
fi
echo -e "\xE2\x9C\x93 Rust config file"

cd $RUST_DIR
if [[ ! -d "build" || -z `find build -name rustc` ]]; then
  export PATH="${ROOT_DIR}/buildtools/cmake/bin/:${PATH}"
  ./configure --enable-rustbuild --target=x86_64-unknown-fuchsia
  ./x.py build --stage 1
fi
echo -e "\xE2\x9C\x93 rustc"
