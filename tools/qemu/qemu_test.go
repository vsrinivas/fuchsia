// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
)

type expected struct {
	cmd []string
	err error
}

func check(t *testing.T, e expected, cmd []string, err error) {
	t.Helper()
	if diff := cmp.Diff(e.err, err, cmp.Comparer(func(x, y error) bool {
		return x == nil && y == nil || x != nil && y != nil && x.Error() == y.Error()
	})); diff != "" {
		t.Errorf("-want, +got: %s", diff)
	}
	if diff := cmp.Diff(e.cmd, cmd); diff != "" {
		t.Errorf("-want, +got: %s", diff)
	}
}

func TestQEMUCommandBuilder(t *testing.T) {
	b := &QEMUCommandBuilder{}

	// No binary set.
	cmd, err := b.Build()
	check(t, expected{
		cmd: []string{},
		err: fmt.Errorf("QEMU binary path must be set."),
	}, cmd, err)

	b.SetBinary("./bin/qemu")

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
			"./bin/qemu",
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
			"./bin/qemu",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
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
			"./bin/qemu",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
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
			"./bin/qemu",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt-2.12,gic-version=host",
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
			"./bin/qemu",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-chardev", "stdio,id=char0,logfile=logfile.txt,signal=off",
			"-serial", "chardev:char0",
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
			"./bin/qemu",
			"-kernel", "./data/qemu-kernel",
			"-initrd", "./data/zircon-a",
			"-machine", "virt-2.12,gic-version=host",
			"-cpu", "host",
			"-enable-kvm",
			"-m", "4096",
			"-smp", "4",
			"-object", "iothread,id=iothread-otherdisk",
			"-drive", "id=otherdisk,file=./data/otherdisk,format=raw,if=none,cache=unsafe,aio=threads",
			"-device", "virtio-blk-pci,drive=otherdisk,iothread=iothread-otherdisk,addr=04.2",
			"-chardev", "stdio,id=char0,logfile=logfile.txt,signal=off",
			"-serial", "chardev:char0",
			"-netdev", "user,id=net0",
			"-device", "virtio-net-pci,netdev=net0,mac=52:54:00:63:5e:7a",
			"-append", "kernel.serial=legacy infra.foo=bar"},
		err: nil,
	}, cmd, err)
}
