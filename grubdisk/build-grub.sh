#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh

toolchain="$ZIRCON_DIR/prebuilt/downloads/x86_64-elf-6.3.0-$(uname)-x86_64/bin"
if [[ ! -d $toolchain ]]; then
  "./$ZIRCON_DIR/scripts/download-toolchain"
fi

if [[ ! -e $FUCHSIA_OUT_DIR/build-objconv/bin/objconv ]]; then
  mkdir "$FUCHSIA_OUT_DIR/build-objconv"
  cd "$FUCHSIA_OUT_DIR/build-objconv"
  wget http://www.agner.org/optimize/objconv.zip
  shasum -c 5442e7bf53e8ed261424e4271262807b7ca9eb2468be7577e4197c8ed1be96b6 objconv.zip
  unzip objconv.zip
  unzip source.zip
  mkdir bin
  g++ -o bin/objconv -O2 *.cpp || exit 1
fi
export PATH="$FUCHSIA_OUT_DIR/build-objconv/bin:$PATH"

mkdir "$FUCHSIA_GRUB_DIR"
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
make
make install

echo "Grub tools are available from $FUCHSIA_GRUB_DIR/bin"
