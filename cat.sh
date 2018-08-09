#!/usr/bin/env sh
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly output=$1
shift 1
for entry in "$@"
do
  echo -n "$(cat ${entry}) "
done > "${output}"
