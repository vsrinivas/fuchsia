#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
magenta_build_dir=$fuchsia_root/out/build-magenta/build-magenta-pc-x86-64
build_dir=$fuchsia_root/out/debug-x86-64

cd $fuchsia_root
$fuchsia_root/packages/gn/gen.py --modules magma-dev
$fuchsia_root/buildtools/ninja -C $build_dir

echo "Recommended bootserver command:"
echo ""
echo "$magenta_build_dir/tools/bootserver $magenta_build_dir/magenta.bin $build_dir/user.bootfs"
echo ""
echo "Recommended loglistener command:"
echo ""
echo "$magenta_build_dir/tools/loglistener | grep --line-buffered -F -f $fuchsia_root/magma/scripts/test_patterns.txt"
echo ""