// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

const cmdline = "kernel.halt-on-panic=true kernel.bypass-debuglog=true"

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
	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           filepath.Join(exDir, "../fuchsia.zbi"),
		AppendCmdline: cmdline,
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// This message indicates that FEMU has successfully come up and that the Fuchsia system is fairly functional.
	i.WaitForLogMessage("[component_manager] INFO: Component manager is starting up...")
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
	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           filepath.Join(exDir, "../fuchsia.zbi"),
		AppendCmdline: cmdline,
		Disks: []emulator.Disk{
			{
				// Doesn't have to be in a "real" disk format, it just has to be there so we can validate that it's detected.
				Path: filepath.Join(exDir, "../fuchsia.zbi"),
				USB:  false,
			},
		},
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// This message indicates that disks have been bound.  This message comes from fshost.
	i.WaitForLogMessage("/dev/class/block/000 ignored")

	// Check that the emulated disk is there.
	i.RunCommand("lsblk")
	i.WaitForLogMessage("/dev/sys/pci/00:03.0/virtio-block/block")
}
