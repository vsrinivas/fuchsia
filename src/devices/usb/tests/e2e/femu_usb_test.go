// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

var cmdline = []string{"kernel.halt-on-panic=true", "kernel.bypass-debuglog=true"}

func execDir(t *testing.T) (string, error) {
	ex, err := os.Executable()
	return filepath.Dir(ex), err
}

func TestFemuWithUSBDisk(t *testing.T) {
	exDir, err := execDir(t)
	if err != nil {
		t.Fatalf("execDir() err: %s", err)
	}
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	// Add XHCI device to femu.
	device.Hw.Hci = "xhci"

	// Add USB disk drive.
	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "test-ums",
		Image:      filepath.Join(exDir, "../fuchsia.zbi"),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "usb-storage"},
	})

	emu := distro.Create(device)
	emu.Start()

	// This message indicates that the usb disk was detected.
	emu.WaitForLogMessage("found USB device")

	// Check that the usb disk is listed by fuchsia.
	emu.RunCommand("lsusb")
	emu.WaitForLogMessage("SUPER  QEMU QEMU USB HARDDRIVE")
}
