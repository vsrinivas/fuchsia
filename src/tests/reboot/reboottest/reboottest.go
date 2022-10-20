// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package reboottest tests reboot cleanliness based on how the OS is being
// rebooted.
package reboottest

import (
	"context"
	"fmt"
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

// Copied from zircon/kernel/include/platform.h
type HaltAction int

const (
	Halt HaltAction = iota
	Reboot
	RebootBootloader
	RebootRecovery
	Shutdown
)

// Copied from zircon/system/public/zircon/boot/crash-reason.h
type CrashReason int

const (
	Invalid CrashReason = iota
	Unknown
	NoCrash
	OOM
	Panic
	SoftwareWatchdog
	UserspaceRootJobTermination
)

// RebootWithCommand is a test helper that boots a qemu instance, reboots it by issuing |cmd|,
// and then verifies the results with the following expectations:
//
// eKind   - Whether it's expected to be a clean or unclean reboot.
// eAction - The expected halt action taken.
// eReason - The expected halt reason.
func RebootWithCommand(t *testing.T, cmd string, eKind ExpectedRebootType, eAction HaltAction, eReason CrashReason) {
	e := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(e, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.Drive = nil
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial, and wait for fshost to start.
	i.WaitForLogMessages([]string{"console.shell: enabled", "fshost started"})

	if arch == emulator.X64 {
		// Ensure the ACPI driver comes up in case our command will need to interact with the platform
		// driver for power operations.
		i.RunCommand("waitfor class=acpi topo=/dev/sys/platform/pt/acpi/acpi-_SB_/acpi-_SB_-passthrough; echo ACPI_READY")
		i.WaitForLogMessage("ACPI_READY")
	}

	// Trigger a reboot in one of the various ways.
	i.RunCommand(cmd)

	if eKind == CleanReboot {
		// Make sure the file system is notified and unmounts.
		i.WaitForLogMessage("fshost shutdown complete")
	}

	// The suggested next action and the reason that this halt happened.
	haltMsg := fmt.Sprintf("platform_halt suggested_action %d reason %d", eAction, eReason)
	i.WaitForLogMessage(haltMsg)

	// See that the target comes back up.
	i.WaitForLogMessage("welcome to Zircon")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
