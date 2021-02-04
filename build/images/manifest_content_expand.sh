#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# manifest_content_expand.sh input-file output-file depfile
#
# Split manifest lines, replacing the source path with the contents of the file
# at source path. This is used to replace .merkle files with their merkleroot
# values to produce merkleroot indices, such as for pkgfs.
#
# example input line:
# sshd-host/0=obj/garnet/bin/sshd-host/sshd-host.meta/meta.far.merkle
# example output line:
# sshd-host/0=cdd425612a8e968e849b9906335db74927bdfbfa3ecaafdcd2bf6585c2dfe284
# merkle path will be added to depfile:
# output-file: obj/garnet/bin/sshd-host/sshd-host.meta/meta.far.merkle

set -eu

readonly input="$1"
readonly output="$2"
readonly depfile="$3"

merkles="$(cat "${input}")"
readonly merkles

for line in $merkles; do
  if [ -n "${line}" ]; then
    pkg="${line%%=*}"
    merkle_path="${line#*=}"
    merkle_root="$(cat "${merkle_path}")"
    printf "%s=%s\n" "${pkg}" "${merkle_root}"
  fi
done > "${output}"

{
  printf "%s:" "${output}"
  for line in $merkles; do
    if [ -n "${line}" ]; then
      merkle_path="${line#*=}"
      printf " %s" "${merkle_path}"
    fi
  done
} > "${depfile}"
