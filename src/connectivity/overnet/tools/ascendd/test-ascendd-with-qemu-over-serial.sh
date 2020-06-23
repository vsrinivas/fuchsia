#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -ex

trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

socat EXEC:"out/default/host_x64/exe.unstripped/ascendd --serial -" EXEC:"fx qemu -Nk -c console.shell=false" &

sleep 30

fx run fuchsia-pkg://fuchsia.com/overnetstack#meta/overnetstack.cmx &

sleep 120
