// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

var cmdline = []string{"kernel.halt-on-panic=true", "kernel.bypass-debuglog=true"}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}

func TestFemu(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()

	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// This message indicates that FEMU has successfully come up and that the Fuchsia system is fairly functional.
	if err = i.WaitForLogMessage("[component_manager] INFO: Component manager is starting up..."); err != nil {
		t.Fatal(err)
	}
}

func TestFemuWithDisk(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()

	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	// This doesn't have to be in a "real" disk format, it just has to be there so we can validate that it's detected.
	device.Hw.Drives = append(device.Hw.Drives, &fvdpb.Drive{
		Id:         "disk00",
		Image:      filepath.Join(exDir, "../fuchsia.zbi"),
		IsFilename: true,
		Device:     &fvdpb.Device{Model: "virtio-blk-pci"},
	})

	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// This message indicates that disks have been bound.  This message comes from fshost.
	if err = i.WaitForLogMessage("/dev/class/block/000 ignored"); err != nil {
		t.Fatal(err)
	}

	// Check that the emulated disk is there.
	if err = i.RunCommand("lsblk"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("/dev/sys/pci/00:03.0/virtio-block/block"); err != nil {
		t.Fatal(err)
	}
}
