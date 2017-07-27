#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

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
  echo "$MAGENTA_BUILD_DIR/bootfs.manifest"
  while IFS='' read pkg; do
    local manifest="$packagedir/$pkg/boot_manifest"
    test -s "$manifest" && echo $manifest
  done < "$FUCHSIA_BUILD_DIR/gen/packages/gn/packages"
}

system-manifests() {
  local manifest="$FUCHSIA_BUILD_DIR/gen/packages/gn/system.bootfs.manifest"
  if [[ ! -s $manifest ]]; then
    echo "fatal: missing $manifest" >&2
    exit 1
  fi
  echo $manifest
  while IFS='' read pkg; do
    local manifest="$packagedir/$pkg/system_manifest"
    test -s "$manifest" && echo $manifest
  done < "$FUCHSIA_BUILD_DIR/gen/packages/gn/packages"
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
  remountsys='
  if [ ! -f /tmp/remounted-sys ]; then
    sysline=$(lsfs -b /system | head -n 1);
    export systemdev="/${sysline#*/}";
    umount /system || exit 1;
    mount $systemdev /system || exit 1;
    touch /tmp/remounted-sys;
  fi;
  if [ ! -f /tmp/mounted-boot ]; then
    if [ $(lsblk  | grep "efi system" | wc -l) -ne 1 ]; then
      echo "ambiguous or missing efi system partition";
      exit 1;
    fi;
    bootline=$(lsblk | grep "efi system");
    export bootdev="/${bootline#*/}";
    echo $bootdev;
    mkdir /efi || exit 1;
    mount $bootdev /efi || exit 1;
    touch /tmp/mounted-boot;
  fi
  '

  fcmd $remountsys || exit 1
}

sftp-batch-updated-efi-files() {
  local stamp="$1"
  find "$MAGENTA_BUILD_DIR/magenta.bin" \
       "$FUCHSIA_BUILD_DIR"/{cmdline,bootdata.bin} \
       -newer $stamp |
  while read f; do
    echo put $f /efi/$(basename $f)
  done
}

mount-writable-parts &

host=$(netaddr --fuchsia)
if [[ $? != 0 ]]; then
  echo "Couldn't resolve host"
  exit 1
fi

update-bootdata
eficmds=$(sftp-batch-updated-efi-files "$syncstamp")
wait
(
  echo progress
  sftp-batch-updated-system-files "$syncstamp"
  echo $eficmds
) | fsftp -b - "[$host]"
touch "$syncstamp"

if [[ -n $eficmds ]]; then
  echo "EFI files were updated, you should reboot"
fi
