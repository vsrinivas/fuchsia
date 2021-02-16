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
	distro, err := emulator.UnpackFrom(filepath.Join(e, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := distro.Delete(); err != nil {
			t.Error(err)
		}
	})
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog")
	device.Drive = nil

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

	if err = i.WaitForLogMessage("initializing platform"); err != nil {
		t.Fatal(err)
	}

	// Make sure the shell is ready to accept commands over serial, and wait for fshost to start.
	if err = i.WaitForLogMessages([]string{"console.shell: enabled", "fshost.cm"}); err != nil {
		t.Fatal(err)
	}

	if arch == emulator.X64 {
		// Ensure the ACPI driver comes up in case our command will need to interact with the platform
		// driver for power operations.
		if err = i.RunCommand("waitfor class=acpi topo=/dev/sys/platform/acpi; echo ACPI_READY"); err != nil {
			t.Fatal(err)
		}
		if err = i.WaitForLogMessage("ACPI_READY"); err != nil {
			t.Fatal(err)
		}
	}

	// Trigger a reboot in one of the various ways.
	if err = i.RunCommand(cmd); err != nil {
		t.Fatal(err)
	}

	if kind == CleanReboot {
		// Make sure the file system is notified and unmounts.
		if err = i.WaitForLogMessage("fshost shutdown complete"); err != nil {
			t.Fatal(err)
		}
	}

	// Is the target rebooting?
	if err = i.WaitForLogMessage("Shutting down debuglog"); err != nil {
		t.Fatal(err)
	}

	// See that the target comes back up.
	if err = i.WaitForLogMessage("welcome to Zircon"); err != nil {
		t.Fatal(err)
	}
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
