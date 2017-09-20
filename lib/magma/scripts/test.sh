#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
tools_path=$fuchsia_root/buildtools
netaddr=$fuchsia_root/out/build-zircon/tools/netaddr
ssh_config="-F $fuchsia_root/out/debug-x86-64/ssh-keys/ssh_config"
test_out=/tmp/test_out

ssh $ssh_config `$netaddr --fuchsia` 'export GTEST_OUTPUT=xml:/data/test_out/ && . /system/bin/magma_autorun'

rm -rf $test_out
mkdir $test_out
scp -q $ssh_config [`$netaddr --fuchsia`]:/data/test_out/*.xml $test_out

echo "Grepping for failures:"
grep failures $test_out/* | grep -v 'failures=\"0\"'
