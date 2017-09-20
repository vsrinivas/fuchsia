#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
build_dir=$fuchsia_root/out/build-vulkancts
dest_dir=/data/vulkancts
netaddr=$fuchsia_root/out/build-zircon/tools/netaddr
ssh_config="-F $fuchsia_root/out/debug-x86-64/ssh-keys/ssh_config"

ssh $ssh_config `$netaddr --fuchsia` mkdir -p $dest_dir
scp $ssh_config $build_dir/external/vulkancts/modules/vulkan/deqp-vk-stripped [`$netaddr --fuchsia`]:$dest_dir/deqp-vk
scp $ssh_config third_party/vulkan-cts/external/vulkancts/mustpass/1.0.1/vk-default.txt [`$netaddr --fuchsia`]:$dest_dir/vk-default.txt
scp $ssh_config -pr $fuchsia_root/third_party/vulkan-cts/external/vulkancts/data/* [`$netaddr --fuchsia`]:$dest_dir
