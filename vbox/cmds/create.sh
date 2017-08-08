#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ ! -e $FUCHSIA_VBOX_VMDK ]]; then
  "${FUCHSIA_VBOX_SCRIPT_DIR}/fbox.sh" build-disk || exit $?
fi

if ! VBoxManage list vms | grep "\"$FUCHSIA_VBOX_NAME\""; then
  VBoxManage createvm --name "${FUCHSIA_VBOX_NAME}" --register
else
	existing=true
fi

if VBoxManage list runningvms | grep "\"$FUCHSIA_VBOX_NAME\"" 2> /dev/null; then
  VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" poweroff
fi

VBoxManage modifyvm "${FUCHSIA_VBOX_NAME}" \
  --memory $FUCHSIA_VBOX_RAM \
  --audio null \
  --audiocontroller hda \
  --acpi on \
  --chipset piix3 \
  --ioapic on \
  --hpet on \
  --pae on \
  --longmode on \
  --cpus $FUCHSIA_VBOX_CPUS \
  --hwvirtex on \
  --vram 128 \
  --firmware efi \
  --nestedpaging on \
  --nic1 "nat" \
  --nictype1 virtio \
  --uart1 "0x03f8" "4" \
  --uartmode1 "server" "$FUCHSIA_VBOX_CONSOLE_SOCK" \
  --usb on \
  --vtxux on \
  --vtxvpid on \
  --largepages on \
  --usbehci off \
  --keyboard usb \
  --mouse usbtablet

if $existing; then
	VBoxManage storagectl "${FUCHSIA_VBOX_NAME}" --name STORAGE --remove > /dev/null 2>&1
fi

VBoxManage storagectl "${FUCHSIA_VBOX_NAME}" --name STORAGE \
  --add sata \
  --controller IntelAHCI \
  --hostiocache on \
  --bootable on

VBoxManage storageattach "${FUCHSIA_VBOX_NAME}" --storagectl STORAGE \
  --port 0 \
  --device 0 \
  --type hdd \
  --nonrotational on \
  --medium "${FUCHSIA_VBOX_VMDK}"
