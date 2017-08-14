#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
build_dir=$fuchsia_root/out/build-vulkancts
cc=$fuchsia_root/`find buildtools -type l -name "clang"`
cxx=$fuchsia_root/`find buildtools -type l -name "clang++"`
ranlib=$fuchsia_root/`find buildtools -name "ranlib"`
strip=$fuchsia_root/`find buildtools -name "strip"`
ar=$fuchsia_root/`find buildtools -name "llvm-ar"`
ranlib=$fuchsia_root/`find buildtools -name "llvm-ranlib"`
sysroot=$fuchsia_root/out/build-magenta/build-magenta-pc-x86-64/sysroot

if [ ! -d "$sysroot" ]; then
	echo "Can't find sysroot: $sysroot"
	exit 1
fi

mkdir -p $build_dir
cd $build_dir
cmake $fuchsia_root/third_party/vulkan-cts -GNinja  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=$tools_path/ninja -DCMAKE_SYSTEM_NAME=Fuchsia -DCMAKE_SYSROOT=$sysroot -DCMAKE_C_COMPILER=$cc -DCMAKE_CXX_COMPILER=$cxx -DCMAKE_AR=$ar -DCMAKE_RANLIB=$ranlib -DCMAKE_C_FLAGS="-m64 --target=x86_64-fuchsia" -DCMAKE_CXX_FLAGS="-m64 --target=x86_64-fuchsia" -DDE_OS=DE_OS_FUCHSIA -DDEQP_TARGET=fuchsia
$tools_path/ninja
$strip $build_dir/external/vulkancts/modules/vulkan/deqp-vk -o $build_dir/external/vulkancts/modules/vulkan/deqp-vk-stripped
cd -
