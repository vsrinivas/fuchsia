#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
platform=${1:-pc-x86-64}
build_dir=$fuchsia_root/out/build-vulkancts-$platform
cc=$fuchsia_root/`find buildtools -type l -name "clang"`
cxx=$fuchsia_root/`find buildtools -type l -name "clang++"`
ranlib=$fuchsia_root/`find buildtools -name "ranlib"`
strip=$fuchsia_root/`find buildtools -name "strip"`
ar=$fuchsia_root/`find buildtools -name "llvm-ar"`
ranlib=$fuchsia_root/`find buildtools -name "llvm-ranlib"`
sysroot=$fuchsia_root/out/build-zircon/build-zircon-$platform/sysroot

if [ ! -d "$sysroot" ]; then
	echo "Can't find sysroot: $sysroot"
	exit 1
fi

if [[ $platform == *"-arm64" ]]; then
	extra_args="-DFUCHSIA_SYSTEM_PROCESSOR=aarch64 -DDE_CPU=DE_CPU_ARM_64 -DDE_PTR_SIZE=8"
else
	extra_args="-DFUCHSIA_SYSTEM_PROCESSOR=x86_64"
fi

pushd $fuchsia_root/third_party/vulkan-cts
python external/fetch_sources.py
popd

# builds the test executable for the host in order to write out test cases
pushd $fuchsia_root/third_party/vulkan-cts
mkdir -p cases
python scripts/build_caselists.py cases
popd

mkdir -p $build_dir
pushd $build_dir
cmake $fuchsia_root/third_party/vulkan-cts -GNinja  -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=$tools_path/ninja -DFUCHSIA_SYSROOT=$sysroot -DCMAKE_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake -DDE_OS=DE_OS_FUCHSIA -DDEQP_TARGET=fuchsia $extra_args
$tools_path/ninja
$strip $build_dir/external/vulkancts/modules/vulkan/deqp-vk -o $build_dir/external/vulkancts/modules/vulkan/deqp-vk-stripped
popd
