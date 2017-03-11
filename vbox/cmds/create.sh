#!/usr/bin/env bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if ! VBoxManage list vms | grep "\"$FUCHSIA_VBOX_NAME\""; then
  VBoxManage createvm --name "${FUCHSIA_VBOX_NAME}" --register
else
	existing=true
fi

if VBoxManage list runningvms | grep "\"$FUCHSIA_VBOX_NAME\"" 2> /dev/null; then
  VBoxManage controlvm "${FUCHSIA_VBOX_NAME}" poweroff
fi

VBoxManage modifyvm "${FUCHSIA_VBOX_NAME}" \
  --paravirtprovider=kvm \
  --memory $FUCHSIA_VBOX_RAM \
  --audio null \
  --audiocontroller hda \
  --acpi on \
  --ioapic on \
  --hpet on \
  --pae on \
  --longmode on \
  --cpuid-portability-level 0 \
  --cpus $FUCHSIA_VBOX_CPUS \
  --hwvirtex on \
  --vram 128 \
  --accelerate3d on \
  --firmware efi \
  --nestedpaging on \
  --nic1 "nat" \
  --nictype1 82540EM \
  --uart1 "0x03f8" "4" \
  --uartmode1 "server" "$FUCHSIA_VBOX_CONSOLE_SOCK" \
  --usb on \
  --vtxux on \
  --vtxvpid on \
  --largepages on \
  --usbehci off \
  --usbxhci off \
  --keyboard usb \
  --mouse usbtablet

if $existing; then
	VBoxManage storagectl "${FUCHSIA_VBOX_NAME}" --name STORAGE --remove
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
  --medium "${FUCHSIA_VBOX_VDI}"
