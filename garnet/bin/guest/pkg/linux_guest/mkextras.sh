#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

usage() {
  echo "usage: ${0} {extras_dir} {target_image} {depfile}"
  exit 1
}

if [ "$#" -ne 3 ]; then
  usage
fi

declare -r EXTRAS_DIR=${1}
declare -r TARGET_IMAGE=${2}
declare -r DEPFILE=${3}
declare -r BLOCK_SIZE=4096
declare -r ADDITIONAL_BLOCKS=1024
declare -r EXTRAS_SIZE=$(du -sb ${EXTRAS_DIR} | grep -o '^[0-9]*')
declare -r EXTRAS_BLOCKS=$(($((${EXTRAS_SIZE}/${BLOCK_SIZE}))+${ADDITIONAL_BLOCKS}))
declare -r INPUT_FILES=$(find "${EXTRAS_DIR}" -type f | tr '\n' ' ')
declare -r DEPFILE_CONTENTS="${TARGET_IMAGE}: ${INPUT_FILES}"
mke2fs -q -d "${EXTRAS_DIR}" -t ext2 -b ${BLOCK_SIZE} "${TARGET_IMAGE}" ${EXTRAS_BLOCKS}
echo "${DEPFILE_CONTENTS}" > ${DEPFILE}
