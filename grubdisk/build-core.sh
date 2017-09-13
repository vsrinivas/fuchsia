#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/env.sh

set -eux

tmpdir=$(mktemp -d $(basename $0).XXXXXX)
if [[ ! -d $tmpdir ]]; then
  echo "Temporary directory creation failure: $tmpdir" >&2
  exit 1
fi
trap "rm -rf $tmpdir" EXIT

modules="
archelp.mod
biosdisk.mod
boot.mod
boot.mod
bufio.mod
bufio.mod
crypto.mod
datetime.mod
extcmd.mod
fat.mod
fshelp.mod
gettext.mod
lsapm.mod
memdisk.mod
mmap.mod
multiboot.mod
net.mod
normal.mod
part_gpt.mod
priority_queue.mod
relocator.mod
search.mod
search_fs_file.mod
search_fs_uuid.mod
search_label.mod
serial.mod
tar.mod
terminal.mod
terminfo.mod
vbe.mod
video.mod
video.mod
video_fb.mod
"

coredir="$tmpdir/core"
mkdir -p $coredir

for f in $modules
do
  cp "$FUCHSIA_GRUB_DIR/lib/grub/i386-pc/$f" "$coredir/"
done

for f in $FUCHSIA_GRUB_DIR/lib/grub/i386-pc/{*.lst,*.img,modinfo.sh}
do
  cp "$f" "$coredir/"
done

memdiskdir="$tmpdir/memdisk"
mkdir -p $memdiskdir/boot/grub

cat <<-EOF > $memdiskdir/boot/grub/grub.cfg
serial --unit=0 --speed=38400
terminal_input serial
terminal_output serial
set timeout=0
menuentry "Zircon" {
  search    --set root --file /zircon.bin --hint hd1,gpt2
  multiboot /zircon.bin gfxconsole.early kernel.debug_uart_poll=1
  module    /bootdata.bin
  boot
}
EOF

tar cf "$tmpdir/memdisk.tar" -C "$memdiskdir" boot

"$FUCHSIA_GRUB_DIR/bin/grub-mkimage" -v \
  --memdisk "$tmpdir/memdisk.tar" \
  --directory "$coredir" \
  --format "i386-pc" \
  --output "$FUCHSIA_GRUB_DIR/core.img" \
  $modules
