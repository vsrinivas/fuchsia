#!/bin/bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is a simple wrapper around copy_tree.py. The wrapper is used to
# limit the allow list in action_tracer.py to just this usage, and not all
# usages of copy_tree.py.

set -eu

# The path to copy_tree.py
script="$1"
# The source directory to copy
source="$2"
# The destination directory to copy into
dest="$3"
# The stampfile copy_tree.py will update
stampfile="$4"

"$script" "$source" "$dest" "$stampfile"
