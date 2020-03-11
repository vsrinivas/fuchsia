// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"testing"
)

func TestAEMUCommandBuilder(t *testing.T) {
	b := &AEMUCommandBuilder{}

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
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
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
			"-feature", "GLDirectMem,KVM",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt,gic_version=host",
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
			"-feature", "GLDirectMem,KVM",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt,gic_version=host",
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
			"-feature", "GLDirectMem,KVM",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt,gic_version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-net", "none",
			"-append", "kernel.serial=legacy infra.foo=bar"},
		err: nil,
	}, cmd, err)

	b.AddNetwork(
		Netdev{
			ID:   "net0",
			User: &NetdevUser{},
			MAC:  "52:54:00:63:5e:7a",
		},
	)

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt,gic_version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-netdev", "user,id=net0",
			"-device", "virtio-net-pci,netdev=net0,mac=52:54:00:63:5e:7a",
			"-append", "kernel.serial=legacy infra.foo=bar"},
		err: nil,
	}, cmd, err)

	b.SetGPU("swiftshader_indirect")

	cmd, err = b.Build()
	check(t, expected{
		cmd: []string{
			"./bin/emulator",
			"-feature", "GLDirectMem,KVM,VULKAN",
			"-gpu", "swiftshader_indirect",
			"-fuchsia",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt,gic_version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-netdev", "user,id=net0",
			"-device", "virtio-net-pci,netdev=net0,mac=52:54:00:63:5e:7a",
			"-append", "kernel.serial=legacy infra.foo=bar"},
		err: nil,
	}, cmd, err)
}
