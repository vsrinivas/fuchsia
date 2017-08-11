#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#
# Clone and build a Linux kernel for use as a guest.

set -e

LINUXDIR=/tmp/linux
DEFCONFIG=machina_defconfig

while getopts "c:d:" OPT; do
  case $OPT in
    c) DEFCONFIG="$OPTARG" ;;
    d) LINUXDIR="$OPTARG" ;;
  esac
done

echo "Building linux with $DEFCONFIG in $LINUXDIR"

# Shallow clone the repository.
if [ ! -d "$LINUXDIR" ]; then
  git clone --depth 1 --branch machina https://magenta-guest.googlesource.com/third_party/linux "$LINUXDIR"
fi

# Update the repository.
cd "$LINUXDIR"
git pull

# Build Linux.
make "$DEFCONFIG"
make -j $(getconf _NPROCESSORS_ONLN)
