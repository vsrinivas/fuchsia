#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Produce a depsfile for the given rsp, then run the command that actually does real work.

depsfile=$1
shift
target_name=$1
shift
rspfile=$1
shift

# Replace line breaks in the rspfile with spaces.
(
  echo -n "${target_name}: "
  while read line; do
    echo -n "${line} "
  done < "${rspfile}"
  echo
) > "${depsfile}"

# Run the build that follows
exec "$@"
