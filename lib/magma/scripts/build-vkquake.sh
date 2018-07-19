#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

build=${1:-debug-x64}
builddir=out/build-vkquake-$build

if [[ $zircon_platform == *"arm64" ]]; then
  arch=arm64
  shared_path=arm64
  system_processor=aarch64
else
  arch=x64
  shared_path=x64
  system_processor=x86_64
fi

export VULKAN_INCLUDE_DIR=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include
export VULKAN_LIB_PATH=$fuchsia_root/out/$build/$shared_path-shared
export VULKAN_LIB=$VULKAN_LIB_PATH/libvulkan.so

sysroot=$fuchsia_root/out/$build/sdks/zircon_sysroot/arch/$arch/sysroot
ninja_path=$fuchsia_root/buildtools/ninja

mkdir -p $builddir
pushd $builddir
cmake $fuchsia_root/third_party/vkQuake/Quake -GNinja -DCMAKE_PREFIX_PATH=$fuchsia_root/out/build-sdl-$build/install -DCMAKE_BUILD_TYPE=Debug -DFUCHSIA_SYSTEM_PROCESSOR=$system_processor -DCMAKE_MAKE_PROGRAM=$ninja_path -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake
$ninja_path
popd
