#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#
# Create a GPT disk image for magenta guests.

set -eo pipefail

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MAGENTADIR="${MAGENTA_DIR:-${GUEST_SCRIPTS_DIR}/../../../..}"
BUILDDIR="${MAGENTA_BUILD_DIR:-$MAGENTADIR/build-magenta-pc-x86-64}"
MINFS="${BUILDDIR}/tools/minfs"

# Magenta's block-watcher will auto mount GPT partitions with this GUID as
# the system partition.
MAGENTA_SYSTEM_GUID="606B000B-B7C7-4653-A7D5-B737332C899D"
MAGENTA_GPT_IMAGE="${BUILDDIR}/magenta.gpt"
MAGENTA_SYSTEM_IMAGE="${BUILDDIR}/system.minfs"

usage() {
    echo "usage: ${0} [-f]"
    echo ""
    echo "    -f Force a rebuild even if the artifact already exists."
    echo ""
    exit 1
}

# sgdisk is used to manipulate GPT partition tables.
check_sgdisk() {
  type -P sgdisk &>/dev/null && return 0

  # sgdisk is provided by the gdisk package.
  echo "Required package gdisk is not installed. (sudo apt install gdisk)"
  exit 1
}

# Create a minfs system image file.
#
# $1 - Image filename.
# $2 - Integer number of MB to make the partition.
generate_system_image() {
  local image=${1}
  local sys_part_size_mb=${2}

  dd if=/dev/zero of="${image}" bs=1M count="${sys_part_size_mb}"
  ${MINFS} ${image} create
  ${MINFS} ${image} mkdir ::/bin

  # Copy binaries from system/uapp into the system image.
  for app_path in `find "${BUILDDIR}/system/uapp" -iname "*.elf"`; do
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

  # sgdisk operates on 512 byte sector addresses.
  local sys_part_size=`du --block-size 512 ${system_image} | cut -f 1`
  local sys_start_sector=2048
  local sys_end_sector=$((${sys_part_size} + ${sys_start_sector}))

  dd if=/dev/zero of="${image}" bs=512 count="$((${sys_end_sector} + 2048))"

  sgdisk --new 1:${sys_start_sector}:${sys_end_sector} ${image}
  sgdisk --typecode 1:${MAGENTA_SYSTEM_GUID}  ${image}
  sgdisk --print ${image}

   # Copy bytes from the system image into the correct location in the GPT
   # image.
   dd if="${system_image}" \
      of="${image}" \
      bs=512 \
      seek="${sys_start_sector}" \
      count=${sys_part_size} \
      conv=notrunc
}

declare FORCE="${FORCE:-false}"

while getopts "f" opt; do
  case "${opt}" in
    f) FORCE="true" ;;
    *) usage ;;
  esac
done

readonly "${FORCE}"

check_sgdisk

# Are the requested targets up-to-date?
if [ "${FORCE}" != "true" ] && [ -f "${MAGENTA_GPT_IMAGE}" ]; then
  echo "GPT image already exists. Pass -f to force a rebuild."
  exit 0
fi

generate_system_image "${MAGENTA_SYSTEM_IMAGE}" "20"
generate_gpt_image "${MAGENTA_GPT_IMAGE}" "${MAGENTA_SYSTEM_IMAGE}"
