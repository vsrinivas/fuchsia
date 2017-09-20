#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`

export VULKAN_INCLUDE_DIR=$fuchsia_root/third_party/vulkan_loader_and_validation_layers/include
export VULKAN_LIBRARY=$fuchsia_root/out/debug-x86-64/x64-shared/libvulkan.so
export VULKAN_LIB_PATH=$fuchsia_root/out/debug-x86-64/x64-shared
unset EDITOR

CONFIG=Debug

FUCHSIA_PLATFORM_OPTIONS="-DCMAKE_MAKE_PROGRAM=/Volumes/CaseSensitive/projects/fuchsia/buildtools/ninja -DFUCHSIA_SYSTEM_PROCESSOR=x86_64 -DFUCHSIA_SYSROOT=$fuchsia_root/out/build-zircon/build-zircon-pc-x86-64/sysroot -DVULKAN_INCLUDE_DIR=$VULKAN_INCLUDE_DIR -DVULKAN_LIBRARY=$VULKAN_LIBRARY"

cd third_party/gfxbench
WORKSPACE=${PWD} PLATFORM=fuchsia CONFIG=$CONFIG FUCHSIA_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake NG_CMAKE_GENERATOR="Ninja" NG_CMAKE_PLATFORM_OPT=$FUCHSIA_PLATFORM_OPTIONS scripts/build-3rdparty.sh
WORKSPACE=${PWD} PLATFORM=fuchsia CONFIG=$CONFIG FUCHSIA_TOOLCHAIN_FILE=$fuchsia_root/build/Fuchsia.cmake NG_CMAKE_GENERATOR="Ninja" NG_CMAKE_PLATFORM_OPT=$FUCHSIA_PLATFORM_OPTIONS DISPLAY_PROTOCOL=MAGMA NO_GL=1 scripts/build.sh
cd -

echo --------------------------------------------------------------------------
echo BUILD COMPLETE
echo
echo 'Copy to persistent storage on device (release build):'
echo 'scp -r -F out/release-x86-64/ssh-keys/ssh_config third_party/gfxbench/tfw-pkg [`out/build-zircon/tools/netaddr --fuchsia`]:/data'
echo
echo 'And execute:'
echo "out/build-zircon/tools/netruncmd : '/data/tfw-pkg/bin/testfw_app -t vulkan_5_normal'"
echo
echo --------------------------------------------------------------------------
