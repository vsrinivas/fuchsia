#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# manifest_content_expand.sh input-file output-file
#
# Split manifest lines, replacing the source path with the contents of the file
# at source path. This is used to replace .merkle files with their merkleroot
# values to produce merkleroot indices, such as for pkgfs.
#
# example input:
# sshd-host/0=obj/garnet/bin/sshd-host/sshd-host.meta/meta.far.merkle
# example output:
# sshd-host/0=cdd425612a8e968e849b9906335db74927bdfbfa3ecaafdcd2bf6585c2dfe284

set -e

readonly input="$1"
readonly output="$2"

while read line
do
  if [ -n "${line}" ]; then
    left="${line%%=*}"
    right=$(cat "${line#*=}")
    printf "%s=%s\n" "$left" "$right"
  fi
done < "${input}" > "${output}"
