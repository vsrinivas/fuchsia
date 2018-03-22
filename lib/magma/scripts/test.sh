#!/bin/bash

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
netaddr=$fuchsia_root/out/build-zircon/tools/netaddr
test_out=/tmp/magma_test_out

scripts/fx shell 'rm -rf /data/magma_test_out; export GTEST_OUTPUT=xml:/data/magma_test_out/ && . /system/bin/magma_autorun'

rm -rf $test_out
mkdir $test_out
scripts/fx scp [`$netaddr --fuchsia`]:/data/magma_test_out/*.xml $test_out

echo "Grepping for failures:"
grep failures $test_out/* | grep -v 'failures=\"0\"'
