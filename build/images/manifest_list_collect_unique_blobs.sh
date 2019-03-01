#!/bin/sh
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# manifest_list_collect_unique_blobs.sh list-input-file manifest-output-file
#
# Given a file containing a list of manifest files, each of which contain dest
# values (left side of =) that is a content-address, sort the content address
# and only emit a single line for a given content address.

set -e

readonly input="$1"
readonly output="$2"

lastprefix="???????"

sort -u $(cat "${input}") | \
while read line
do
  if [ -z "${line}" ]; then
    continue
  fi

  left="${line%%=*}"
  if [ "${lastprefix}" = "${left}" ]; then
    continue
  fi
  lastprefix="${left}"

  echo "${line}"
done > "${output}"
