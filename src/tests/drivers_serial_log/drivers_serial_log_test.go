// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

// Most stringent set of permission, which should still enable serial logs
var cmdline = []string{
	"console.shell=false",
	"kernel.enable-debugging-syscalls=false",
	"kernel.enable-serial-syscalls=output-only",
	"devmgr.log-to-debuglog=false",
}

func TestSerialLogsAvailable(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
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
	// The bringup zbi. See //build/images/bringup/BUILD.gn
	device.Initrd = "fuchsia"
	device.Drive = nil

	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for a log from driver framework (driver framework is just one of the many modules
	// routed to serial logs).
	i.WaitForLogMessage("Launching driver_host 'driver_host:sys'")

}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
