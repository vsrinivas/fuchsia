#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#
# Download and build toybox to be an initrd for linux.
#
# .config is "make defconfig" with the following additions:
#  + sh (toysh)
#
# More additions to come as and when desired / needed.

SRCFILE="$PWD/${BASH_SOURCE[0]}"

# Location of the patch that we'll apply to the default config
PATCHFILE="${SRCFILE%/*}/tb-defconfig_plus.patch"

# Location of magenta and its build dir
MAGENTADIR="${SRCFILE%magenta/*}magenta"
BUILDDIR="${MAGENTA_BUILD_DIR:-$MAGENTADIR/build-magenta-pc-x86-64}"
mkdir -p $BUILDDIR

# Location to download tarballs to
PULLDIR="${1:-/tmp}"

# Where to build toybox
IRDIR="$BUILDDIR/initrd-x86"

# Where to prep the initrd fs
IRDIR_FS="$IRDIR/fs"

# The toybox version we're building
TBVERSION="0.7.4"

# Where the toybox srcs are expected to be
TBDOWNLOAD="$PULLDIR/toybox-$TBVERSION"

# Check for a built initramfs in the output
if [ ! -f "$IRDIR/initrd.gz" ]; then
  echo "No image in $IRDIR, making one..."

  # Not built? Do we even have the source?
  if [ ! -d "$TBDOWNLOAD" ]; then
    echo "Downloading toybox to $TBDOWNLOAD"
    if curl https://landley.net/toybox/downloads/toybox-$TBVERSION.tar.gz | \
        tar xz -C "$PULLDIR"; then
      echo "We got the box"
    else
      echo "Some issues getting the box!"
      exit 1
    fi
  fi

  # From here on, errors should just terminate the script.
  set -e

  mkdir -p "$IRDIR_FS"/{bin,sbin,etc,proc,sys,usr/{bin,sbin}}

  make -C "$TBDOWNLOAD" defconfig

  # Apply the patch to include the shell
  echo "Applying config patches..."
  patch "$TBDOWNLOAD/.config" << '_EOF'
--- .config.old 2017-07-22 03:04:04.335638468 +1000
+++ .config 2017-07-22 03:04:18.391546765 +1000
@@ -159,7 +159,7 @@
 # CONFIG_PING is not set
 # CONFIG_ROUTE is not set
 # CONFIG_SETFATTR is not set
-# CONFIG_SH is not set
+CONFIG_SH=y
 # CONFIG_CD is not set
 # CONFIG_EXIT is not set
 # CONFIG_SULOGIN is not set
_EOF

  LDFLAGS="--static" make -C "$TBDOWNLOAD" -j100
  PREFIX=$IRDIR_FS make -C "$TBDOWNLOAD" install

  # Write an init script for toybox.
  cat > "$IRDIR_FS/init" <<'_EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
echo Launched toybox
/bin/sh
_EOF

  chmod +x "$IRDIR_FS/init"

  (cd "$IRDIR_FS" && find . -print0 \
    | cpio --null -o --format=newc \
    | gzip -9 > $IRDIR/initrd.gz)
else
  echo "initrd.gz found. Doing nothing."
  echo "To force a rebuild, \"rm $IRDIR/initrd.gz\""
fi
