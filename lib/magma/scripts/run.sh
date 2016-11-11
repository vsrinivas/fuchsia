#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
magenta_build_dir=$fuchsia_root/out/build-magenta/build-magenta-pc-x86-64
netcp=$magenta_build_dir/tools/netcp
netruncmd=$magenta_build_dir/tools/netruncmd
build_dir=$fuchsia_root/out/Debug/x64-shared

test=$1
if [ "$test" != "" ]
    then $netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
    exit
fi

vulkan_tests=true;
if $vulkan_tests; then
	test="state_pool"
	$netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
	test="state_pool_no_free"
	$netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
	test="state_pool_free_list_only"
	$netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
	test="block_pool_no_free"
	$netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
	test="test_wsi_magma"
	$netcp $build_dir/$test :/tmp/$test && $netruncmd magenta /tmp/$test
fi
