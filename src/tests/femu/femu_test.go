// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

var cmdline = []string{"kernel.halt-on-panic=true", "kernel.bypass-debuglog=true"}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}

func TestFemu(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	// This message indicates that FEMU has successfully come up and that the Fuchsia system is fairly functional.
	i.WaitForLogMessage("[component_manager] INFO: Component manager is starting up...")
}

func TestFemuWithDisk(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	// This doesn't have to be in a "real" disk format, it just has to be there so we can validate that it's detected.
	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "disk00",
		Image:      filepath.Join(exDir, "../fuchsia.zbi"),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "virtio-blk-pci"},
	})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	// This message indicates that disks have been bound.  This message comes from fshost.
	i.WaitForLogMessage("/dev/class/block/000 ignored")

	// Check that the emulated disk is there.
	i.RunCommand("lsblk")
	i.WaitForLogMessage("/pci-00:03.0-fidl/virtio-block/block")
}
