#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

usage() {
  echo "usage: $(basename $0) [netaddr args (hostname)]" >&2
}

case "$1" in
  -h|--help|help)
    usage
    exit
    ;;
esac

# Find the directory that this script lives in.
if [[ -n "${ZSH_VERSION}" ]]; then
  thisdir=${${(%):-%x}:a:h}
else
  thisdir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
# Our parent dir is $FUCHSIA_DIR/scripts.
scriptsdir="$(dirname "${thisdir}")"

if [[ -z "${FUCHSIA_BUILD_DIR+x}" || ! -d "${FUCHSIA_BUILD_DIR}" ]]; then
    # FUCHSIA_BUILD_DIR is unset or doesn't point to a directory. Although we
    # source env.sh below, we can't guess which args the user would want to pass
    # to fset.
    echo 'fatal: This script requires that you define FUCHSIA_BUILD_DIR, or:' >&2
    echo "       - source ${scriptsdir}/env.sh" >&2
    echo '       - run "fset" with the desired args' >&2
    exit 1
fi

# This script uses some env.sh functions, which won't be defined here even
# if the calling shell has them.
source "${scriptsdir}/env.sh"

packagedir="$FUCHSIA_BUILD_DIR/package"
bootdata="$FUCHSIA_BUILD_DIR/bootdata.bin"
syncstamp="$FUCHSIA_BUILD_DIR/sync.stamp"

boot-manifests() {
  echo "$FUCHSIA_BUILD_DIR/boot.manifest"
}

system-manifests() {
  echo "$FUCHSIA_BUILD_DIR/system.manifest"
}

# Parse a manifest file, give as arg or stdin, extracting source or target
manifest-sources() {
  grep -v '^#' ${1:-"-"} | cut -d = -f 2-
}
manifest-targets() {
  grep -v '^#' ${1:-"-"} | cut -d = -f 1
}

updated-sources() {
  local manifests=$(boot-manifests)
  find $(cat $manifests | manifest-sources) -newer $bootdata
}

need-new-bootdata() {
  if [[ ! -s $bootdata ]]; then
    return 0
  fi

  local manifests=$(boot-manifests)
  if [[ -n $(find $manifests -newer $bootdata) ]]; then
    return 0
  fi

  if [[ -n $(find $(cat $manifests | manifest-sources) -newer $bootdata) ]]; then
    return 0
  fi

  return 1
}

update-bootdata() {
  local manifests=$(boot-manifests)
  if need-new-bootdata "$bootdata"; then
    mkbootfs -o "$bootdata" --target=boot $(boot-manifests)
    return 0
  fi
  return 1
}

updated-system-manifest() {
  local stamp="$1"
  cat $(system-manifests) | while read -r line; do
    if [[ ${line#*=} -nt $stamp ]]; then
      echo $line
    fi
  done
}

sftp-batch-updated-system-files() {
  local stamp="$1"
  updated-system-manifest "$stamp" | while read -r line; do
    echo put ${line#*=} /system/${line%=*}
  done
}

mount-writable-parts() {
  scp -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config "$thisdir/remount.sh" "[$1]:/tmp/remount.sh" || exit 1

  fx ssh $1 /boot/bin/sh /tmp/remount.sh || exit 1
}

sftp-batch-updated-efi-files() {
  local stamp="$1"
  local files=(
    "$ZIRCON_BUILD_DIR/zircon.bin"
    "$FUCHSIA_BUILD_DIR/cmdline"
    "$FUCHSIA_BUILD_DIR/bootdata.bin"
  )
  for f in "${files[@]}"; do
    local added=0
    # Only include files that exist.
    if [[ -f "$f" ]]; then
      # If there's a stamp file, only include files newer than it.
      if [[ ! -e "$stamp" || "$f" -nt "$stamp" ]]; then
        echo "info: including $f" >&2
        echo put $f /efi/$(basename $f)
        added=1
      fi
    fi
    if (( !added )); then
        echo "info: skipping $f" >&2
    fi
  done
}

cd $FUCHSIA_BUILD_DIR

echo "finding address of host"
host=$(netaddr --fuchsia $@)
if [[ $? != 0 ]]; then
  echo "Couldn't resolve host"
  exit 1
fi

echo "remounting partitions as writable"
mount-writable-parts $host || exit 1

echo "updating bootdata"
update-bootdata

echo "syncing updated files"
eficmds=$(sftp-batch-updated-efi-files "$syncstamp")
(
  echo progress
  sftp-batch-updated-system-files "$syncstamp"
  echo $eficmds
) | fx sftp -b - "[$host]"
touch "$syncstamp"

if [[ -n $eficmds ]]; then
  echo "EFI files were updated, you should reboot"
fi

fx shell umount /efi
