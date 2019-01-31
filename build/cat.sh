#!/bin/sh
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# cat.sh output_file [file, ...]
# cat.sh concatenates all file arguments into output_file, separated by lines.
# cat.sh elides empty lines (in order to avoid breaking the symbolizer)

# Note: `cat $output $@` is not equivalent - that does not produce new lines
# between inputs.

readonly output="$1"
shift 1
for file in "$@"
do
  val="$(<"${file}")"
  if [ -n "${val}" ]; then
    echo "${val}"
  fi
done > "${output}"
