// Copyright 2022 The Fuchsia Authors. All rights reserved.
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

var cmdline = []string{"kernel.halt-on-panic=true", "kernel.bypass-debuglog=true", "installer.non-interactive=true", "zvb.current_slot=_r"}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}

func TestInstaller(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.Initrd = "recovery-installer"
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	// Create a new empty disk to install to.
	f, err := os.CreateTemp("", "recovery-installer")
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(f.Name())

	// Make it 64GB.
	err = f.Truncate(64 * 1024 * 1024 * 1024)
	if err != nil {
		t.Fatal(err)
	}

	// Enable xHCI in the emulator.
	device.Hw.Hci = "xhci"

	// The installer disk is connected as a UMS device.
	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "installer",
		Image:      filepath.Join(exDir, "../obj/build/images/installer/installer.img"),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "usb-storage", Options: []string{"drive=installer", "bus=xhci.0"}},
	})

	// The target install disk is connected as a PCI device.
	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "disk",
		Image:      f.Name(),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "virtio-blk-pci"},
	})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	// This message indicates that disks have been bound.  This message comes from fshost.
	i.WaitForLogMessage("/dev/class/block/008 ignored")
	i.RunCommand("lsblk")

	// Wait for install to be finished.
	i.RunCommand("log_listener &")
	i.WaitForLogMessage("Set active configuration to 1")

	i.RunCommand("dm shutdown")
	i.WaitForLogMessage("fshost shutdown complete")
	cancel()

	// Now, we try booting the installed image.
	device = emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "disk",
		Image:      f.Name(),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "virtio-blk-pci"},
	})

	ctx, cancel = context.WithCancel(context.Background())
	defer cancel()
	i = distro.CreateContext(ctx, device)
	i.Start()

	// This message indicates that virtcon successfully started.
	i.WaitForLogMessage("vc: started with args VirtualConsoleArgs")
}
