#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is a helper script that creates a fuchsia volume appropriate for OTA
# testing.

set -e

PARTITION_SIZE=$(( 2 * 1024 * 1024 * 1024))
FUCHSIA_IMG=""

while (($#)); do
  case "$1" in
    --partition-size)
      PARTITION_SIZE="$2"
      shift
      ;;
    -i|--image)
      FUCHSIA_IMG="$2"
      shift
      ;;
    *)
      echo "Unrecognized option: $1"
      exit 1
  esac
  shift
done

if [[ -z "${PARTITION_SIZE}" ]]; then
  echo "--partition-size cannot be empty"
  exit 1
fi

if [[ -z "${FUCHSIA_IMG}" ]]; then
  echo "--image must be specified"
  exit 1
fi

set -u

TMPDIR="$(mktemp -d)"
if [[ ! -d "${TMPDIR}" ]]; then
  echo >&2 "Failed to create temporary directory"
  exit 1
fi
trap 'rm -r "${TMPDIR}"' EXIT

# This isn't strictly necessary, but it will make sure that the output is sent
# to the serial, and that we'll skip trying to netboot.
cat << EOF > "${TMPDIR}/cmdline.txt"
kernel.serial=legacy
TERM=xterm-256color
kernel.halt-on-panic=true
bootloader.default=local
bootloader.timeout=0
EOF

exec fx make-fuchsia-vol \
  -cmdline "${TMPDIR}/cmdline.txt" \
  -resize "$PARTITION_SIZE" \
  -include-keys \
  "${FUCHSIA_IMG}"
