#!/bin/sh
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cat $1 > $2
cat $1 > $3
cat $1 > $4

# Commenting out one of the above lines should induce an error:
# Missing writes:
#   ...
