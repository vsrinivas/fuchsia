#!/bin/bash

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

build=${1:-debug-x64}
builddir=out/build-doom3

if [[ $build == *"arm64" ]]; then
  arch=arm64
  shared_path=arm64
  system_processor=aarch64
else
  arch=x64
  shared_path=x64-shared
  system_processor=x86_64
fi

if [[ $build == "debug"* ]]; then
  cmake_build_type="Debug"
else
  cmake_build_type="Release"
fi

export VULKAN_INCLUDE_DIR=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include
export SDL_INCLUDE_DIR=$fuchsia_root/third_party/sdl/include
export FUCHSIA_LIB_DIR=$fuchsia_root/out/$build/$shared_path

sysroot=$fuchsia_root/out/$build/sdks/zircon_sysroot/arch/$arch/sysroot
ninja_path=$fuchsia_root/buildtools/ninja

mkdir -p $builddir
pushd $builddir
cmake $fuchsia_root/third_party/RBDOOM-3-BFG/neo -GNinja -DVULKAN=TRUE -DFFMPEG=FALSE -DCMAKE_BUILD_TYPE=$cmake_build_type -DFUCHSIA_SYSTEM_PROCESSOR=$system_processor -DCMAKE_MAKE_PROGRAM=$ninja_path -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake
$ninja_path
popd
