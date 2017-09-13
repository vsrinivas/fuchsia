#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if ! which VBoxManage 2>&1 > /dev/null ; then
  echo "VBoxManage (virtualbox command line tools) must be on your PATH." >&2
  exit 1
fi

if [[ -z $FUCHSIA_SCRIPTS_DIR ]]; then
  source $( cd $( dirname "${BASH_SOURCE[0]}" ) && pwd)/../env.sh
fi

# VBoxManage makes RPCs to a running virtualbox instance, but the locks
# sometimes long outlast the RPC completion, so making VBoxManage sleepy avoids
# those issues.
VBOXMANAGE=`which VBoxManage`
VBoxManage() {
  "${VBOXMANAGE}" "$@"
  sleep 0.5
}

export FUCHSIA_VBOX_NAME=${FUCHSIA_VBOX_NAME:-fuchsia}
export FUCHSIA_VBOX_CPUS
export FUCHSIA_VBOX_RAM=${FUCHSIA_VBOX_RAM:-4096}
export FUCHSIA_VBOX_VRAM=${FUCHSIA_VBOX_VRAM:-128}
export FUCHSIA_VBOX_DISK_SIZE=${FUCHSIA_VBOX_DISK_SIZE:-4g}
export FUCHSIA_VBOX_RAW=${FUCHSIA_VBOX_RAW:-$FUCHSIA_OUT_DIR/vbox/disk.raw}
export FUCHSIA_VBOX_VMDK=${FUCHSIA_VBOX_VMDK:-$FUCHSIA_OUT_DIR/vbox/disk.vmdk}
export FUCHSIA_VBOX_CONSOLE_SOCK=${FUCHSIA_VBOX_CONSOLE_SOCK:-"$FUCHSIA_OUT_DIR/vbox/${FUCHSIA_VBOX_NAME}.sock"}

# XXX(raggi): Virtualbox and Zircon are not getting along with serial
# interrupts. Moving to 1 CPU provides sane serial performance for now.
# if [[ -z ${FUCHSIA_VBOX_CPUS} ]]; then
# 	ncpu=$(getconf _NPROCESSORS_ONLN || echo 4)
# 	# VirtualBox only suppots up to 32 CPUs
# 	if [[ $ncpu -gt 32 ]]; then
# 		ncpu=32
# 	fi
# 	FUCHSIA_VBOX_CPUS=$ncpu
# fi
FUCHSIA_VBOX_CPUS=1