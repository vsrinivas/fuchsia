#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh

set -e

toolchain="${FUCHSIA_DIR}/zircon/prebuilt/downloads/x86_64-elf-6.3.0-$(uname)-x86_64/bin"
if [[ ! -d $toolchain ]]; then
  "./${FUCHSIA_DIR}/zircon/scripts/download-toolchain"
fi

if [[ ! -e $FUCHSIA_OUT_DIR/build-objconv/bin/objconv ]]; then
  mkdir -p "$FUCHSIA_OUT_DIR/build-objconv"
  cd "$FUCHSIA_OUT_DIR/build-objconv"
  curl -O http://www.agner.org/optimize/objconv.zip
  got=$(shasum -a 256 objconv.zip | cut -d ' ' -f 1)
  want="475a0d68e041485ecbd638289fb4304a28a87974a0ac38a7c71eba9692af8bf8"
  if [[ "$want" != "$got" ]]; then
    echo -e "shasum for objconv didn't match:\nwant: $want\ngot:  $got\n" >&2
    exit 1
  fi
  unzip objconv.zip
  unzip source.zip
  mkdir -p bin
  g++ -o bin/objconv -O2 *.cpp
fi
export PATH="$FUCHSIA_OUT_DIR/build-objconv/bin:$PATH"

mkdir -p "$FUCHSIA_GRUB_DIR"
cd "$FUCHSIA_GRUB_DIR"
git clone git://git.savannah.gnu.org/grub.git
cd grub
git checkout 007f0b407f72314ec832d77e15b83ea40b160037
if [[ ! -f configure ]]; then
  ./autogen.sh
fi
if [[ ! -f Makefile ]]; then
  ./configure --target=x86_64-elf --prefix="$FUCHSIA_GRUB_DIR" \
    TARGET_CC=$toolchain/x86_64-elf-gcc TARGET_OBJCOPY=$toolchain/x86_64-elf-objcopy \
    TARGET_STRIP=$toolchain/x86_64-elf-strip TARGET_NM=$toolchain/x86_64-elf-nm \
    TARGET_RANLIB=$toolchain/x86_64-elf-ranlib
fi
make &&
make install

echo "Grub tools are available from $FUCHSIA_GRUB_DIR/bin"
