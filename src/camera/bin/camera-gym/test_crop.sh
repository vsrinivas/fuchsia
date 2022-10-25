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

while true; do
  ffx component explore $moniker -c "camera-gym-ctl --set-config=0 --add-stream=0 --add-stream=1 --add-stream=2"
  sleep 2

  ffx component explore $moniker -c "camera-gym-ctl --set-config=1 --add-stream=0 --add-stream=1"
  sleep 2

  for p in {0..39}
  do
    f=`echo ${p}.0 / 40.0 | bc -l | sed -e 's/00*$/0/'`
    crop_x=`echo ${f}       | bc -l | sed -e 's/00*$/0/'`
    crop_y=`echo ${f}       | bc -l | sed -e 's/00*$/0/'`
    crop_w=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    crop_h=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    ffx component explore $moniker -c "camera-gym-ctl --set-crop=1,${crop_x},${crop_y},${crop_w},${crop_h}"
  done
  for p in {0..39}
  do
    f=`echo ${p}.0 / 40.0 | bc -l | sed -e 's/00*$/0/'`
    crop_x=`echo 0.0        | bc -l | sed -e 's/00*$/0/'`
    crop_y=`echo 0.0        | bc -l | sed -e 's/00*$/0/'`
    crop_w=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    crop_h=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    ffx component explore $moniker -c "camera-gym-ctl --set-crop=1,${crop_x},${crop_y},${crop_w},${crop_h}"
  done

  ffx component explore $moniker -c "camera-gym-ctl --set-config=2 --add-stream=0 --add-stream=1"
  sleep 2

  for p in {0..39}
  do
    f=`echo ${p}.0 / 40.0 | bc -l | sed -e 's/00*$/0/'`
    crop_x=`echo ${f}       | bc -l | sed -e 's/00*$/0/'`
    crop_y=`echo ${f}       | bc -l | sed -e 's/00*$/0/'`
    crop_w=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    crop_h=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    ffx component explore $moniker -c "camera-gym-ctl --set-crop=1,${crop_x},${crop_y},${crop_w},${crop_h}"
  done
  for p in {0..39}
  do
    f=`echo ${p}.0 / 40.0 | bc -l | sed -e 's/00*$/0/'`
    crop_x=`echo 0.0        | bc -l | sed -e 's/00*$/0/'`
    crop_y=`echo 0.0        | bc -l | sed -e 's/00*$/0/'`
    crop_w=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    crop_h=`echo 1.0 - ${f} | bc -l | sed -e 's/00*$/0/'`
    ffx component explore $moniker -c "camera-gym-ctl --set-crop=1,${crop_x},${crop_y},${crop_w},${crop_h}"
  done

done
