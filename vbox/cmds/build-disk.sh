#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

mkdir -p $(dirname $FUCHSIA_VBOX_VDI)

if [[ ! -e ${FUCHSIA_VBOX_SOURCE_DISK} ]]; then
  echo "$FUCHSIA_VBOX_SOURCE_DISK not found, building..."
  "${FUCHSIA_SCRIPTS_DIR}/installer/build-installable-userfs.sh"
fi

if [[ -e ${FUCHSIA_VBOX_VDI} ]]; then
	uuid=$(VBoxManage showmediuminfo out/vbox/efi_fs.vdi  | grep UUID: | head -n 1 | awk '{print $2}')
	if [[ -n "${uuid}" ]]; then
		uuid="--uuid=${uuid}"
		rm "${FUCHSIA_VBOX_VDI}"
	fi
fi

VBoxManage convertfromraw $uuid "${FUCHSIA_VBOX_SOURCE_DISK}" "${FUCHSIA_VBOX_VDI}"
