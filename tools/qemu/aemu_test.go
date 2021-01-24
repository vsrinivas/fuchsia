// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"testing"
)

func TestAEMUCommandBuilder(t *testing.T) {
	b := NewAEMUCommandBuilder()

	// No binary set.
	cmd, err := b.Build()
	check(t, expected{
		cmd: []string{},
		err: fmt.Errorf("QEMU binary path must be set."),
	}, cmd, err)

	b.SetBinary("./bin/emulator")

	// No kernel set.
	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{},
		err: fmt.Errorf("QEMU kernel path must be set."),
	}, cmd, err)

	b.SetKernel("./data/qemu-kernel")

	// No initrd set.
	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{},
		err: fmt.Errorf("QEMU initrd path must be set."),
	}, cmd, err)

	b.SetInitrd("./data/zircon-a")

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-net", "none"},
		err: nil,
	}, cmd, err)

	b.SetTarget(TargetEnum.AArch64, true)
	b.SetMemory(4096)
	b.SetCPUCount(4)

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-net", "none"},
		err: nil,
	}, cmd, err)

	b.AddVirtioBlkPciDrive(
		Drive{
			ID:   "otherdisk",
			File: "./data/otherdisk",
			Addr: "04.2",
		},
	)

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-net", "none"},
		err: nil,
	}, cmd, err)

	b.AddKernelArg("kernel.serial=legacy")
	b.AddKernelArg("infra.foo=bar")

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-net", "none",
			"-append", "infra.foo=bar kernel.serial=legacy"},
		err: nil,
	}, cmd, err)

	b.AddNetwork(
		Netdev{
			ID:   "net0",
			User: &NetdevUser{},
			Device: Device{
				Model:   DeviceModelVirtioNetPCI,
				options: []string{"mac=52:54:00:63:5e:7a"},
			},
		},
	)

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-netdev", "user,id=net0",
			"-device", "virtio-net-pci,mac=52:54:00:63:5e:7a,netdev=net0",
			"-append", "infra.foo=bar kernel.serial=legacy"},
		err: nil,
	}, cmd, err)

	b.AddSerial(
		Chardev{
			ID:      "char0",
			Logfile: "logfile.txt",
			Signal:  false,
		},
	)

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VirtioInput,Vulkan",
			"-gpu", "swiftshader_indirect",
			"-no-window",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-vga", "none",
			"-device", "virtio-keyboard-pci",
			"-device", "virtio_input_multi_touch_pci_1",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-netdev", "user,id=net0",
			"-device", "virtio-net-pci,mac=52:54:00:63:5e:7a,netdev=net0",
			"-chardev", "stdio,id=char0,logfile=logfile.txt,signal=off,echo=off",
			"-serial", "chardev:char0",
			"-append", "infra.foo=bar kernel.serial=legacy"},
		err: nil,
	}, cmd, err)
}
