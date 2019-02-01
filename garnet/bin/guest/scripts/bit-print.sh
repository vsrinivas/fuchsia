#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

BIT=0
VAL=$1

while (($VAL != 0)); do
    printf "%3d: %d\n" $BIT $((VAL & 1))
    ((VAL >>= 1))
    ((BIT++))
done
