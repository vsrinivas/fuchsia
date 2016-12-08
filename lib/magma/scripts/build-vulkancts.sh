#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
build_dir=$fuchsia_root/out/build-vulkancts

mkdir -p $build_dir
cd $build_dir
$tools_path/cmake/bin/cmake $fuchsia_root/third_party/vulkan-cts -GNinja  -DCMAKE_MAKE_PROGRAM=$tools_path/ninja -DCMAKE_SYSTEM_NAME=Fuchsia -DCMAKE_SYSROOT=$fuchsia_root/out/sysroot/x86_64-fuchsia -DCMAKE_C_COMPILER=$tools_path/toolchain/clang+llvm-x86_64-darwin/bin/clang -DCMAKE_CXX_COMPILER=$tools_path/toolchain/clang+llvm-x86_64-darwin/bin/clang++ ../../buildtools/cmake/bin/cmake ../../third_party/vulkan-cts -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-m64 --target=x86_64-fuchsia" -DCMAKE_CXX_FLAGS="-m64 --target=x86_64-fuchsia" -DDE_OS=DE_OS_FUCHSIA -DCMAKE_AR=$tools_path/toolchain/clang+llvm-x86_64-darwin/bin/llvm-ar -DCMAKE_RANLIB=$tools_path/toolchain/clang+llvm-x86_64-darwin/bin/llvm-ranlib -DDEQP_TARGET=fuchsia
$tools_path/ninja && $tools_path/toolchain/clang+llvm-x86_64-darwin/bin/strip $build_dir/external/vulkancts/modules/vulkan/deqp-vk -o $build_dir/external/vulkancts/modules/vulkan/deqp-vk-stripped && ls -l $build_dir/external/vulkancts/modules/vulkan/deqp-vk*
cd -
