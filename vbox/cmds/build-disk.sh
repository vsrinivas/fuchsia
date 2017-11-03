#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

mkdir -p $(dirname $FUCHSIA_VBOX_RAW)

mfv=$FUCHSIA_BUILD_DIR/tools/make-fuchsia-vol

if [[ ! -x $mfv ]]; then
	echo "You need to build the 'make-fuchsia-vol' package" >&2
	exit 1
fi

if [[ ! -e $FUCHSIA_VBOX_RAW ]]; then
	echo "Allocating raw image space"
	case $(uname) in
		Linux)
			fallocate -l $FUCHSIA_VBOX_DISK_SIZE $FUCHSIA_VBOX_RAW
			;;
		Darwin)
			mkfile -n $FUCHSIA_VBOX_DISK_SIZE $FUCHSIA_VBOX_RAW
			;;
		*)
			echo "Unsupported platform" >&2
			exit 1
			;;
	esac
fi

if [[ ! -e $FUCHSIA_VBOX_VMDK ]]; then
	VBoxManage internalcommands createrawvmdk -filename ${FUCHSIA_VBOX_VMDK} -rawdisk ${FUCHSIA_VBOX_RAW}
fi

if [[ ! -e $FUCHSIA_BUILD_DIR/cmdline ]]; then
	echo "$FUCHSIA_BUILD_DIR/cmdline is not present. Populate it to set a kernel command line"
fi

# builds/updates the disk image:
if ! "$mfv" "$@" "$FUCHSIA_VBOX_RAW" ; then
	echo "Raw disk image build failed" >&2
	exit 1
fi
