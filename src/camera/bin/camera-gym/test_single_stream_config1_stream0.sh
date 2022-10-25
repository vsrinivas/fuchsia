#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Testing the camera-gym-ctl:
#
# This sequence assumes:
#
# 1. "fx serve-remote ..." or equivalent is running.
# 2. "fx vendor google camera-gym-manual" is started.

count=`fx shell ps | grep camera-gym-manual | wc -l`
if [ ${count} != 1 ]
then
  echo 'ERROR: Manual instance of camera-gym not detected'
  exit 1
fi

moniker=`ffx --machine json component show camera-gym-manual.cm | fx jq -r '.[0].moniker'
echo $moniker

ffx component explore $moniker -c "camera-gym-ctl --set-config=1 --add-stream=0"
