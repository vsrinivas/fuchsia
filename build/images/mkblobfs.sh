#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BLOBFS_TOOL=$1
shift
BLOBFS_IMAGE=$1
shift

# remove the previously generated file.
rm -f $BLOBFS_IMAGE

# then run blobfs with no previously-written-to file present.
$BLOBFS_TOOL $@
