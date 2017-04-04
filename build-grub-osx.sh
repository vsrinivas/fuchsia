#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ $(uname) != "Darwin" ]]; then
	echo "This script is for macOS"
	exit 1
fi

export elf_toolchain=$MAGENTA_DIR/prebuilt/downloads/x86_64-elf-6.2.0-Darwin-x86_64

export PATH=$elf_toolchain/bin:$PATH

if [[ ! -e $MAGENTA_TOOLS_DIR/objconv ]]; then
	mkdir $FUCHSIA_OUT_DIR/build-objconv
	cd $FUCHSIA_OUT_DIR/build-objconv
	wget http://www.agner.org/optimize/objconv.zip
	shasum -c 5442e7bf53e8ed261424e4271262807b7ca9eb2468be7577e4197c8ed1be96b6 objconv.zip
	unzip objconv.zip
	unzip source.zip
	g++ -o objconv -O2 *.cpp
	install -m 755 objconv $MAGENTA_TOOLS_DIR
fi

mkdir $FUCHSIA_OUT_DIR/build-grub
cd $FUCHSIA_OUT_DIR/build-grub
git clone git://git.savannah.gnu.org/grub.git
cd grub
if [[ ! -f configure ]]; then
	./autogen.sh
fi
if [[ ! -f Makefile ]]; then
	./configure --target=x86_64-elf --prefix=$FUCHSIA_OUT_DIR/build-grub \
		TARGET_CC=x86_64-elf-gcc TARGET_OBJCOPY=x86_64-elf-objcopy \
		TARGET_STRIP=x86_64-elf-strip TARGET_NM=x86_64-elf-nm \
		TARGET_RANLIB=x86_64-elf-ranlib
fi
make
make install
for f in $FUCHSIA_OUT_DIR/build-grub/bin/*; do
	ln -s $f $MAGENTA_TOOLS_DIR/$(basename $f)
done
