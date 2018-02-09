#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

build=${1:-debug-x86-64}
builddir=out/build-sdl-$build

if [[ $build == *"aarch64" ]]; then
	shared_path=arm64
	system_processor=aarch64

else
	shared_path=x64
	system_processor=x86_64
fi

export FUCHSIA_INCLUDE_PATH=$fuchsia_root/garnet/lib/magma/include/magma_abi
export FUCHSIA_LIB_PATH=$fuchsia_root/out/$build/$shared_path-shared
export SDL_VULKAN_HEADER=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include/vulkan/vulkan.h

sysroot=$fuchsia_root/out/$build/sdks/zircon_sysroot/sysroot
ninja_path=$fuchsia_root/buildtools/ninja

mkdir -p $builddir
pushd $builddir
cmake $fuchsia_root/third_party/sdl -GNinja -DVIDEO_VULKAN=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_PREFIX=install -DCMAKE_BUILD_TYPE=Debug -DFUCHSIA_SYSTEM_PROCESSOR=$system_processor -DCMAKE_MAKE_PROGRAM=$ninja_path -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake 
$ninja_path install
popd
