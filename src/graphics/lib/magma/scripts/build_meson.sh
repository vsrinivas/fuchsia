#!/bin/bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is useful for building Magma on Linux with an external toolchain.
# It will be used in the build process for virtualized Linux to produce
# 64bit and 32bit libraries, using the same toolchain used to build the
# Magma clients.
#
# It streamlines the virtualization build because a full jiri checkout
# isn't required. It's also handy for quick Magma on Linux experimentation.
#
# To keep the same source code structure, we define the meson project at
# the Fuchsia (top) level; however, to avoid confusion we keep all meson
# files inside magna and link to them from the top level.
#

set -e

basedir=${PWD##*/}
outdir="out/magma_meson"

with_tests=false
if [ "${1}" == "--with-tests" ]; then
	with_tests=true
	echo "Building with tests"
fi

if [ ! -d "src/graphics/lib/magma" ]; then
	echo Error: must be executed from toplevel fuchsia directory
	exit 1
fi

if [ ! -L "meson.build" ]; then
	ln -s src/graphics/lib/magma/meson-top/meson.build meson.build
fi

if [ ! -L "meson_options.txt" ]; then
	ln -s src/graphics/lib/magma/meson-top/meson_options.txt meson_options.txt
fi

if [ ! -d ${outdir} ]; then
	mkdir -p ${outdir}
	meson ${outdir}
fi

meson configure ${outdir} -Dwith_tests=${with_tests}

ninja -C ${outdir}
