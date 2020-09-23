#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="${GUEST_SCRIPTS_DIR}/../../.."
cd "${FUCHSIA_DIR}"

# Check for the required tools.
which dtc > /dev/null || {
  echo "${0} requires dtc"
  echo "sudo apt install device-tree-compiler"
  exit 1
}

which iasl > /dev/null || {
  echo "${0} requires iasl"
  echo "sudo apt install acpica-tools"
  exit 1
}

# Generate device tree blobs.
MAX_SIZE=8192
for DTS in src/virtualization/bin/vmm/arch/arm64/dts/*.dts; do
  DTB=${DTS%.dts}.dtb
  dtc $DTS -o $DTB -S $MAX_SIZE

  SIZE=$(stat --printf=%s ${DTB})
  if [ $SIZE -ne $MAX_SIZE ]; then
    echo "Device tree blob, ${DTB}, is too large"
    exit 1
  fi
done

# Generate ACPI blobs.
for ASL in src/virtualization/bin/vmm/arch/x64/asl/*.asl; do
  iasl -vs -we $ASL
done
