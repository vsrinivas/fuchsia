#!/usr/bin/env sh
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

mkdir -p logs

while true; do
    now=$(date +%FT%T%z)
    echo "${now}"
    fx shell memgraph -tvH > "logs/${now}"
    sleep 10m
done
