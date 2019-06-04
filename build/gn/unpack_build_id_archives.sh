#!/bin/sh

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# Directory in which the prebuilt build-id archives are found.
build_id_archive_dir="${1}"
# Directory at which to unpack all of the build-id archives.
unpack_dir="${2}"
# The path at which to output the list of archives.
output="${3}"

archives=""
if [ -d "$build_id_archive_dir" ]
then
  archives="$(find "$build_id_archive_dir" -name "*.tar.bz2" -follow)"
fi

if [ ! -d "$unpack_dir" ]
then mkdir "$unpack_dir"
fi

if [ ! -z "$array" ]
then echo "$archives" | xargs --max-procs 10 -n1 tar -xC "$unpack_dir" -jf
fi

printf "%s\n" "$archives" > "$output"
