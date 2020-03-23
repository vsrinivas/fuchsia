#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eu # Error checking

err_print() {
  echo "Error on line $1"
}

trap 'err_print $LINENO' ERR

DEBUG_LINE() {
    "$@"
}

# Ensure list of hosts tests is present
HOST_TESTS_TXT="$1"
if [ ! -f "$HOST_TESTS_TXT" ]; then
  echo "Error: Could not find hosts test file in $HOST_TESTS_TXT"
  exit 1;
fi

echo
echo "==== Run host tests ===="
while IFS= read -r testname
do
  chmod +x "${testname}"
  "${testname}"
done < "${HOST_TESTS_TXT}"
echo
echo "Success!"
