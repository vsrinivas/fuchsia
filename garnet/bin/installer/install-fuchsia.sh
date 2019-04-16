#!/boot/bin/sh

# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

INSTALL_PATH="/install"
PAVER="/boot/bin/install-disk-image"

# TODO(raggi): template this from the build instead.
IMAGES="fvm.sparse.blk fuchsia.esp.blk fuchsia.zbi zedboot.zbi"

if [ ! -e "${PAVER}" ]; then
  echo "Paver \"install-disk-image\" is missing!"
  exit 1
fi

for file in $IMAGES; do
  img="${INSTALL_PATH}/${file}"

  if [ ! -f ${img} ]; then
    continue
  fi

  typ=""
  case "$file" in
    fvm.sparse.blk)
      typ="fvm"
      ;;
    fuchsia.esp.blk)
      typ="bootloader"
      ;;
    fuchsia.zbi)
      typ="zircona"
      ;;
    zedboot.zbi)
      typ="zirconr"
      ;;
    *)
      echo "Unknown paver image type for $file" >&2
      exit 1
      ;;
  esac

  set -x
  "${PAVER}" install-${typ} --file "${img}" || exit $?
  set +x
done

echo -n reboot > /dev/misc/dmctl
