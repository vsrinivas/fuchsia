// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package reboottest tests reboot cleanliness based on how the OS is being
// rebooted.
package reboottest

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

// ExpectedRebootType declares what is the expectation for the reboot
// cleanliness.
type ExpectedRebootType int

const (
	CleanReboot = iota
	UncleanReboot
)

// RebootWithCommand is a test helper that boots a qemu instance then reboots
// it by issuing cmd.
func RebootWithCommand(t *testing.T, cmd string, kind ExpectedRebootType, zbi_name string) {
	e := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(e, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog")
	device.Drive = nil
	i := distro.Create(device)
	i.Start()

	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial, and wait for fshost to start.
	i.WaitForLogMessages([]string{"console.shell: enabled", "fshost"})

	if arch == emulator.X64 {
		// Ensure the ACPI driver comes up in case our command will need to interact with the platform
		// driver for power operations.
		i.RunCommand("waitfor class=acpi topo=/dev/sys/platform/acpi; echo ACPI_READY")
		i.WaitForLogMessage("ACPI_READY")
	}

	// Trigger a reboot in one of the various ways.
	i.RunCommand(cmd)

	if kind == CleanReboot {
		// Make sure the file system is notified and unmounts.
		i.WaitForLogMessage("fshost shutdown complete")
	}

	// Is the target rebooting?
	i.WaitForLogMessage("Shutting down debuglog")

	// See that the target comes back up.
	i.WaitForLogMessage("welcome to Zircon")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
