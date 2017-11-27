#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

zircon_platform=${1:-pc-x86-64}

if [[ $zircon_platform == *"-arm64" ]]; then
	platform=aarch64
	shared_path=arm64
	system_processor=aarch64

else
	platform=x86-64
	shared_path=x64
	system_processor=x86_64
fi

export VULKAN_INCLUDE_DIR=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include
export VULKAN_LIB_PATH=$fuchsia_root/out/debug-$platform/$shared_path-shared
export VULKAN_LIB=$VULKAN_LIB_PATH/libvulkan.so
#unset EDITOR

sysroot=$fuchsia_root/out/build-zircon/build-zircon-$zircon_platform/sysroot
ninja_path=$fuchsia_root/buildtools/ninja

cd third_party/vkQuake
mkdir -p build
pushd build
cmake ../Quake -GNinja -DCMAKE_PREFIX_PATH=$fuchsia_root/third_party/sdl/install -DCMAKE_BUILD_TYPE=Debug -DFUCHSIA_SYSTEM_PROCESSOR=$system_processor -DCMAKE_MAKE_PROGRAM=$ninja_path -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake 
$ninja_path
popd
