#!/usr/bin/env bash

# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#
# Download and build a tiny linux kernel as a magenta hypervisor guest.
#
# .config is "make alldefconfig" with the following additions:
#  + Initial RAM filesystem and RAM disk (initramfs/initrd) support
#  + 8250/16550 and compatible serial support
#  + Console on 8250/16550 and compatible serial port
#  + kexec
#
# More additions to come as and when desired / needed.

SRCFILE="$PWD/${BASH_SOURCE[0]}"

# Location of the patches that we'll apply to the default config
PATCHFILE="${SRCFILE%/*}/alldefconfig_plus.patch"

# Location of magenta and its build dir
MAGENTADIR="${SRCFILE%magenta/*}magenta"
BUILDDIR="${MAGENTA_BUILD_DIR:-$MAGENTADIR/build-magenta-pc-x86-64}"
mkdir -p $BUILDDIR

# Location to download tarballs to
PULLDIR="${1:-/tmp}"

# Where to build the kernel
LINUXDIR="$BUILDDIR/linux-x86"

# The kernel version we're building
LINUXVERSION="4.9.30"

# Where the kernel srcs are expected to be
LINUXDOWNLOAD="$PULLDIR/linux-$LINUXVERSION"

# Check for a built linux in the output
if [ ! -f "$LINUXDIR/vmlinux" ]; then
  echo "No linux in $LINUXDIR, making one..."

  # Not built? Do we even have the source?
  if [ ! -d "$LINUXDOWNLOAD" ]; then
    echo "Downloading linux to $LINUXDOWNLOAD"
    if curl https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-$LINUXVERSION.tar.xz | \
        tar xJf - -C "$PULLDIR"; then
      echo "We got the linux"
    else
      echo "Some issues getting the linux!"
      exit 1
    fi
  fi

  mkdir -p "$LINUXDIR"

  # alldefconfig is pretty small (allnoconfig is smaller, but it needs more tweaks)
  make -C "$LINUXDOWNLOAD" O="$LINUXDIR" alldefconfig

  # Apply our patches
  patch "$LINUXDIR/.config" "$PATCHFILE"

  make -C "$LINUXDOWNLOAD" O="$LINUXDIR" -j100
else
  echo "vmlinux found. Doing nothing."
  echo "To force a rebuild, \"rm $LINUXDIR/vmlinux\""
fi
