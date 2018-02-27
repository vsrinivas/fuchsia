#!/bin/bash

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

build=${1:-debug-x86-64}
builddir=out/build-doom3-$build

if [[ $build == *"aarch64" ]]; then
	shared_path=arm64
	system_processor=aarch64

else
	shared_path=x64
	system_processor=x86_64
fi

if [[ $build == "debug"* ]]; then
	cmake_build_type="Debug"
else
	cmake_build_type="Release"
fi

export VULKAN_INCLUDE_DIR=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include
export FUCHSIA_LIB_DIR=$fuchsia_root/out/$build/$shared_path-shared

sysroot=$fuchsia_root/out/$build/sdks/zircon_sysroot/sysroot
ninja_path=$fuchsia_root/buildtools/ninja

mkdir -p $builddir
pushd $builddir
cmake $fuchsia_root/third_party/RBDOOM-3-BFG/neo -GNinja -DVULKAN=TRUE -DFFMPEG=FALSE -DCMAKE_PREFIX_PATH=$fuchsia_root/out/build-sdl-$build/install -DCMAKE_BUILD_TYPE=$cmake_build_type -DFUCHSIA_SYSTEM_PROCESSOR=$system_processor -DCMAKE_MAKE_PROGRAM=$ninja_path -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake
$ninja_path
popd
