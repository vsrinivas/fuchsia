#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} {target_image} [FILES...]"
  exit 1
}

echo $@

if [ "$#" -lt 2 ]; then
  usage
fi

declare -r TARGET_IMAGE=${1}
declare -r BLOCK_SIZE=4096
declare -r EXTRAS_SIZE=52428800
declare -r EXTRAS_BLOCKS=$((${EXTRAS_SIZE}/${BLOCK_SIZE}))
mke2fs -q -t ext2 -E root_owner=1000:1000 -b ${BLOCK_SIZE} "${TARGET_IMAGE}" ${EXTRAS_BLOCKS}

shift
for f in "$@"
do
  e2cp -P 777 -G 1000 -O 1000 "${f}" "${TARGET_IMAGE}:/$(basename ${f})"
done

