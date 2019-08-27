#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

echo "${STDOUT_STRING}"
echo "${STDERR_STRING}" >&2

stdin=""
while read line; do
  stdin="${line}"
  break
done

mkdir -p /root/output
echo "${stdin}" > /root/output/script_output.txt
