#! /bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script runs the given command line, after cutting off the first argument
# and using it as a stamp file.

_stamp_file="$1"
shift
"${@}" && touch "$_stamp_file"

