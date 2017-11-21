#!/boot/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [ ! -f /tmp/mounted-boot ]; then
  if [ $(lsblk  | grep "efi system" | wc -l) -ne 1 ]; then
    echo "ambiguous or missing efi system partition"
    exit 1
  fi;
  export bootdev="/dev/class/block/$(lsblk | grep "efi system" | cut -d " " -f 1)"
  echo Boot device: $bootdev
  mkdir /efi || exit 1
  mount $bootdev /efi || exit 1
  touch /tmp/mounted-boot
fi

if [ ! -f /tmp/remounted-sys ]; then
  sysline=$(df /system | grep block/part)
  systemdev=$(echo $sysline | cut -d " " -f 7)
  if [ -z $(echo $systemdev | grep "/dev/") ]; then
    echo "failed to identify system device, found $systemdev"
    exit 1
  fi
  blockdev=$(lsblk | grep "$systemdev" | cut -d " " -f 1)
  echo "system device: $blockdev"
  umount /system || exit 1
  mount /dev/class/block/$blockdev /system || exit 1
  touch /tmp/remounted-sys
fi