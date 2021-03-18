#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Test to make sure fsatrace is detecting reads and writes as expected.
# If this fails, then all bets are off for action-tracing analysis.
# Run this from $FUCHSIA_DIR, the source checkout root.
# Usage:
#   $0 [fsatrace-path [stamp-file]]
# Standard exit code.

set -e

if test "$#" -ge 1
then fsatrace="$1"
else fsatrace=./prebuilt/fsatrace/fsatrace
fi

# Absolute path.
fsatrace="$(readlink -f "$fsatrace")"

# If a stamp file is named, touch it at the end of this script.
stamp=
if test "$#" -ge 2
then
  stamp="$2"
  rm -f "$stamp"
fi

# Note: To work on BSD and Linux mktemp, -t must be attached to the template.
tmpd="$(mktemp -d -t tmp.fsatrace_test.XXXXX)"

### Test: file copy
echo "hello, world" > "$tmpd"/src.txt
cp="$(which cp)"
"$fsatrace" erwmdtq "$tmpd/cp_trace.txt" -- cp "$tmpd"/src.txt "$tmpd"/dest.txt

# Use exit code of diff.
diff -u "$tmpd/cp_trace.txt" - <<EOF
r|$cp
q|$tmpd/src.txt
r|$tmpd/src.txt
q|$tmpd/src.txt
w|$tmpd/dest.txt
q|$tmpd/dest.txt
EOF
# If there is a difference, -e will exit here.

### Test: remove symlink
# See https://github.com/jacereda/fsatrace/issues/41
# On Linux, removing a symlink *looks* like removing the link's target.

rm="$(which rm)"
pushd "$tmpd" > /dev/null 2>&1
touch __temp__.txt
ln -s __temp__.txt __link__.txt
"$fsatrace" erwmdtq "$tmpd/rm_trace.txt" -- rm -f __link__.txt
rm -f __temp__.txt
popd > /dev/null 2>&1

diff -u "$tmpd/rm_trace.txt" - <<EOF
r|$rm
q|$tmpd/__temp__.txt
d|$tmpd/__temp__.txt
EOF
# If there is a difference, -e will exit here.

# Touch a stamp file if one is requested.
test -z "$stamp" || touch "$stamp"

