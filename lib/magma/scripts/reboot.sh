#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
fuchsia_root=`pwd`
magenta_build_dir=$fuchsia_root/out/build-magenta/build-magenta-pc-x86-64
netcp=$magenta_build_dir/tools/netcp
netruncmd=$magenta_build_dir/tools/netruncmd

$netruncmd : 'dm reboot'
