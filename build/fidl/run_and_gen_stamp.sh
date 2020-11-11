#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


# This script wraps a binary that does not generate any output file, but
# can return a status (success or fail). Ninja actions need an output to
# resolve dependencies, so this wrapper creates or updates a stamp file
# with the date/time of the last success.

# Stamp file to touch after the command runs successfully
stamp="$1"
shift

# Execute the command and check return status
"$@"
status=$?
# only update the stamp file if the command returned a successful status
if (( $status == 0 )); then
  touch "${stamp}"
fi

exit ${status}
