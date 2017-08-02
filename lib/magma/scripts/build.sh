#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
magenta_build_dir=$fuchsia_root/out/build-magenta/build-magenta-pc-x86-64
build=debug
bootfs=$fuchsia_root/out/user.bootfs

if [ "$1" == "--debug" ]; then
	build=debug
else 
if [ "$1" == "--release" ]; then
	build=release
else 
if [ "$1" != "" ]; then
	echo Unrecognized arg: $1
	exit 1
fi
fi
fi

build_dir=$fuchsia_root/out/$build-x86-64

args="msd_intel_wait_for_flip=true"
args="$args magma_enable_tracing=false"

modules="magma-dev"
#modules="$modules,tracing,runtime_config,escher"

rm -f $bootfs
cd $fuchsia_root
$fuchsia_root/packages/gn/gen.py --modules $modules --$build --args="$args" --ignore-skia --goma
$fuchsia_root/buildtools/ninja -C $build_dir
cp $build_dir/user.bootfs $bootfs

echo "Recommended bootserver command:"
echo ""
echo "$magenta_build_dir/tools/bootserver $magenta_build_dir/magenta.bin $fuchsia_root/out/user.bootfs -- magenta.autorun.system=/system/bin/magma_autorun"
echo ""
echo "Recommended loglistener command:"
echo ""
echo "$magenta_build_dir/tools/loglistener | grep --line-buffered -F -f $fuchsia_root/magma/scripts/test_patterns.txt"
echo ""
