#!/boot/bin/sh

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script version of install-fuchsia exists because the /install filesystem
# (indeed all minfs filesystems) can no longer provide executable binaries.

INSTALL_PATH="/install"
PAVER="/boot/bin/install-disk-image"

if [ ! -f "${INSTALL_PATH}/fvm.sparse.blk" ]; then
  echo "Installer files are missing!"
  exit 1
fi

if [ ! -e "${PAVER}" ]; then
  echo "Paver \"install-disk-image\" is missing!"
  exit 1
fi

if [ -f "${INSTALL_PATH}/fvm.sparse.blk" ]; then
 if ! "${PAVER}" install-fvm --file "${INSTALL_PATH}/fvm.sparse.blk"; then
  exit $?
 fi
fi

for file in "${INSTALL_PATH}/local-*.esp.blk"; do
  if ! "${PAVER}" install-efi --file "${file}"; then
    exit $?
  fi
done

for file in "${INSTALL_PATH}/zircon-*.vboot"; do
  if ! "${PAVER}" install-kernc --file "${file}"; then
    exit $?
  fi
done