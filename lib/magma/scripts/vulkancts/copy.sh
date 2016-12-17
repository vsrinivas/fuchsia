#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
build_dir=$fuchsia_root/out/build-vulkancts
dest_dir=/data/vulkancts

$fuchsia_root/out/build-magenta/tools/netcp $build_dir/external/vulkancts/modules/vulkan/deqp-vk-stripped :$dest_dir/deqp-vk
$fuchsia_root/out/build-magenta/tools/netcp $build_dir/executor/executor :$dest_dir/executor
$fuchsia_root/out/build-magenta/tools/netcp $build_dir/execserver/execserver :$dest_dir/execserver
$fuchsia_root/out/build-magenta/tools/netcp third_party/vulkan-cts/cases/dEQP-VK-cases.xml :$dest_dir/dEQP-VK-cases.xml
$fuchsia_root/out/build-magenta/tools/netcp magma/scripts/vulkancts/run.sh :$dest_dir/run.sh

cd $fuchsia_root/third_party/vulkan-cts/external/vulkancts/data
find . -type f | xargs -I % $fuchsia_root/out/build-magenta/tools/netcp % :$dest_dir/%
cd -
