#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

ZIRCON_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${ZIRCON_GUEST_DIR}/../../../../../out"
MINFS="${BUILD_DIR}/build-zircon/tools/minfs"

usage() {
    echo "usage: ${0} {x64|arm64}"
    echo ""
    echo "    -o Output directory for image files."
    echo ""
    exit 1
}

while getopts "o:" opt; do
  case "${opt}" in
    o) OUT_DIR=${OPTARG} ;;
    *) usage ;;
  esac
done
shift $((OPTIND - 1))

case "${1}" in
arm64|x64)
  ARCH="$1";;
*)
  usage;;
esac

declare -r OUT_DIR="${OUT_DIR:-${BUILD_DIR}}"
# Zircon's block-watcher will auto mount GPT partitions with this GUID as
# the system partition.
declare -r ZIRCON_SYSTEM_GUID="606B000B-B7C7-4653-A7D5-B737332C899D"
declare -r ZIRCON_GPT_IMAGE="${OUT_DIR}/zircon.gpt"
declare -r ZIRCON_SYSTEM_IMAGE="${OUT_DIR}/system.minfs"
declare -r CGPT="${BUILD_DIR}/${ARCH}/tools/cgpt"

# Create a minfs system image file.
#
# $1 - Image filename.
# $2 - Integer number of MB to make the partition.
generate_system_image() {
  local image=${1}
  local sys_part_size_mb=${2}

  ${MINFS} ${image}@${sys_part_size_mb}m create
  ${MINFS} ${image} mkdir ::/bin

  # Copy binaries from system/uapp into the system image.
  for app_path in `find "${BUILD_DIR}/build-zircon/build-${ARCH}/system/uapp" -iname "*.elf"`; do
    local exe_name=`basename "${app_path}"`
    # Strip the '.elf' file extension.
    local app="${exe_name%.*}"
    ${MINFS} ${image} cp "${app_path}" ::/bin/${app}
  done
}

# Creates a GPT disk image file with a single system partition.
#
# $1 - GPT image name.
# $2 - System partition image path.
generate_gpt_image() {
  local image=${1}
  local system_image=${2}

  # cgpt operates on 512 byte sector addresses.
  local sys_part_size=`du --block-size 512 ${system_image} | cut -f 1`
  local sys_start_sector=2048
  local sys_end_sector=$((${sys_part_size} + ${sys_start_sector}))

  dd status=none \
     if=/dev/zero \
     of="${image}" \
     bs=512 \
     count="$((${sys_end_sector} + 2048))"

  # Pipe stderr to /dev/null because cgpt will always warn us (rightly) that
  # our GPT partition headers are invalid.
  ${CGPT} create ${image} \
      2> /dev/null
  ${CGPT} add \
      -b ${sys_start_sector} \
      -s ${sys_part_size} \
      -t ${ZIRCON_SYSTEM_GUID} \
      ${image}

  # Copy bytes from the system image into the correct location in the GPT
  # image.
  dd status=none \
     if="${system_image}" \
     of="${image}" \
     bs=512 \
     seek="${sys_start_sector}" \
     count=${sys_part_size} \
     conv=notrunc
}

generate_system_image "${ZIRCON_SYSTEM_IMAGE}" "30"
generate_gpt_image "${ZIRCON_GPT_IMAGE}" "${ZIRCON_SYSTEM_IMAGE}" > /dev/null
