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

set -eo pipefail

GUEST_SCRIPTS_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Where the toybox sources are expected to be.
TOYBOX_SRC_DIR="/tmp/toybox"

# Toybox initrd file.
TOYBOX_INITRD="$TOYBOX_SRC_DIR/initrd.gz"

# Toybox root filesystem image.
TOYBOX_ROOTFS="$TOYBOX_SRC_DIR/rootfs.ext2"

# Where to prep the toybox directory structure.
TOYBOX_SYSROOT="$TOYBOX_SRC_DIR/fs"

usage() {
    echo "usage: ${0} [-rif] [-d toybox_dir]"
    echo ""
    echo "    -r Build ext2 filesystem image."
    echo "    -i Build initrd CPIO archive."
    echo "    -f Force a rebuild even if the artifact already exists."
    echo "    -d Directory to clone toybox into."
    echo ""
    exit 1
}

# Ensures the toybox sources are downloaded.
#
# $1 - Directory to unpack the sources into.
get_toybox_source() {
  local toybox_src=$1

  if [ ! -d "$toybox_src" ]; then
    git clone --depth 1 --branch machina https://magenta-guest.googlesource.com/third_party/toybox "$toybox_src"
  fi
}

# Build toybox and create a sysroot.
#
# $1 - Toybox source directory.
# $2 - Directory to build the toybox sysroot with the toybox binary and symlinks.
build_toybox() {
  local toybox_src=$1
  local sysroot_dir="$2"

  make -C "$toybox_src" defconfig

  LDFLAGS="--static" make -C "$toybox_src" -j100

  mkdir -p "$sysroot_dir"/{bin,sbin,etc,proc,sys,usr/{bin,sbin}}
  PREFIX=$sysroot_dir make -C "$toybox_src" install
}

# Generate a simple init script at /init in the target sysroot.
#
# $1 - Toybox source directory.
# $2 - Toybox sysroot directory.
generate_init() {
  local toybox_src="$1"
  local sysroot_dir="$2"

  # Write an init script for toybox.
  cat > "$sysroot_dir/init" <<'_EOF'
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
echo Launched toybox
/bin/sh
_EOF

  chmod +x "$sysroot_dir/init"
}

# Generate a gzipped CPIO archive of the toybox sysroot.
#
# $1 - Toybox sysroot directory.
# $2 - Filepath of the created initrd.
package_initrd() {
  local sysroot="$1"
  local initrd="$2"
  (cd "$sysroot" && find . -print0 \
    | cpio --null -o --format=newc \
    | gzip -9 > $initrd)
}

# e2tools provides utilities for manipulating EXT2 filesystems.
check_e2tools() {
  type -P e2cp &>/dev/null && return 0

  echo "Required package e2tools is not installed. (sudo apt install e2tools)"
  exit 1
}

# Generate an EXT2 filesystem image of the toybox sysroot.
#
# $1 - Toybox sysroot directory.
# $2 - Filepath of the created EXT2 image file.
package_rootfs() {
  local sysroot="$1"
  local rootfs="$2"

  dd if=/dev/zero of=$rootfs bs=1M count=20
  mkfs.ext2 -F $rootfs

  for dir in `find "${sysroot}" -type d -printf '%P\n'`; do
    e2mkdir "${rootfs}:/${dir}"
  done

  for file in `find "${sysroot}" -type f -printf '%P\n'`; do
    e2cp -p -G 0 -O 0 "${sysroot}/${file}" "${rootfs}:/${file}"
  done

  # e2cp follows symlinks which would create a copy of the toybox binary for
  # every link. To work around this we enumerate all the symlinks in the
  # sysroot and create a corresponding hardlink in the ext2 filesystem (e2ln
  # does not currently support soft links).
  for link in `find "${sysroot}" -type l -printf '%P\n'`; do
    local dirname=`dirname ${link}`
    local target=`readlink "${sysroot}/${link}"`
    e2ln "${rootfs}:${dirname}/${target}" "/${link}"
  done
}

declare FORCE="${FORCE:-false}"
declare BUILD_INITRD="${BUILD_INITRD:-false}"
declare BUILD_ROOTFS="${BUILD_ROOTFS:-false}"

while getopts "fird:" opt; do
  case "${opt}" in
    f) FORCE="true" ;;
    i) BUILD_INITRD="true" ;;
    r) BUILD_ROOTFS="true" ;;
    d) TOYBOX_SRC_DIR="${OPTARG}" ;;
    *) usage ;;
  esac
done

# Do we have something to build?
if [[ ! "${BUILD_INITRD}" = "true" ]] && [[ ! "${BUILD_ROOTFS}" = "true" ]]; then
  echo "Either -r or -i is required."
  usage
fi

# Are the requested targets up-to-date?
if [[ ! "${FORCE}" = "true" ]]; then
  if [[ -f "${TOYBOX_INITRD}" ]]; then
    BUILD_INITRD="false"
  fi
  if [[ -f "${TOYBOX_ROOTFS}" ]]; then
    BUILD_ROOTFS="false"
  fi
fi
if [[ ! "${BUILD_INITRD}" = "true" ]] && [[ ! "${BUILD_ROOTFS}" = "true" ]]; then
  echo "All targets up to date. Pass -f to force a rebuild."
  exit 0
fi

readonly "${FORCE}" "${BUILD_INITRD}" "${BUILD_ROOTFS}"

if [[ "${BUILD_ROOTFS}" = "true" ]]; then
  check_e2tools
fi

get_toybox_source "${TOYBOX_SRC_DIR}"

build_toybox "${TOYBOX_SRC_DIR}" "${TOYBOX_SYSROOT}"

generate_init "${TOYBOX_SRC_DIR}" "${TOYBOX_SYSROOT}"

if [[ "${BUILD_INITRD}" = "true" ]]; then
  package_initrd "${TOYBOX_SYSROOT}" "${TOYBOX_INITRD}"
  echo "initrd at ${TOYBOX_INITRD}"
fi

if [[ "${BUILD_ROOTFS}" = "true" ]]; then
  package_rootfs "${TOYBOX_SYSROOT}" "${TOYBOX_ROOTFS}"
  echo "filesystem image at ${TOYBOX_ROOTFS}"
fi
