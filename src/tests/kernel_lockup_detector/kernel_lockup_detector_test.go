// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

var common_args = []string{
	"kernel.lockup-detector.heartbeat-period-ms=50",
	// By default, disable all of the thresholds (both fatal and non-fatal).  During each of the
	// tests, we will re-enable only the specific threshold we want to enable.
	"kernel.lockup-detector.heartbeat-age-threshold-ms=0",
	"kernel.lockup-detector.critical-section-threshold-ms=0",
	"kernel.lockup-detector.heartbeat-age-fatal-threshold-ms=0",
	"kernel.lockup-detector.critical-section-fatal-threshold-ms=0",
	// Upon booting run "k", which will print a usage message.  By waiting for the usage
	// message, we can be sure the system has booted and is ready to accept "k" commands.
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

func TestKernelLockupDetectorCriticalSection(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	// Enable the lockup detector.
	//
	device.KernelArgs = append(device.KernelArgs, common_args...)
	device.KernelArgs = append(device.KernelArgs,
		"kernel.lockup-detector.critical-section-threshold-ms=200")
	d := distro.Create(device)

	// Boot.
	d.Start()

	// Wait for the system to finish booting.
	d.WaitForLogMessage("usage: k <command>")

	// Force a lockup and see that an OOPS is emitted.
	d.RunCommand("k lockup test_critical_section 1 600")
	d.WaitForLogMessage("locking up CPU")
	d.WaitForLogMessage("ZIRCON KERNEL OOPS")
	d.WaitForLogMessage("CPU-1 in critical section for")
	d.WaitForLogMessage("done")
}

func TestKernelLockupDetectorHeartbeat(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, common_args...)
	device.KernelArgs = append(device.KernelArgs,
		"kernel.lockup-detector.heartbeat-age-threshold-ms=200")
	d := distro.Create(device)

	// Boot.
	d.Start()

	// Wait for the system to finish booting.
	d.WaitForLogMessage("usage: k <command>")

	// Force a lockup and see that a heartbeat OOPS is emitted.
	d.RunCommand("k lockup test_spinlock 1 1000")
	d.WaitForLogMessage("locking up CPU")
	d.WaitForLogMessage("ZIRCON KERNEL OOPS")
	d.WaitForLogMessage("no heartbeat from CPU-1")
	// See that the CPU's run queue is printed and contains the thread named "lockup-test", the
	// one responsible for the lockup.
	d.WaitForLogMessage("lockup-test")
	d.WaitForLogMessage("done")
}

func TestKernelLockupDetectorFatalCriticalSection(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	// Enable the lockup detector.
	//
	device.KernelArgs = append(device.KernelArgs, common_args...)
	device.KernelArgs = append(device.KernelArgs,
		"kernel.lockup-detector.critical-section-fatal-threshold-ms=500")
	d := distro.Create(device)

	// Boot.
	d.Start()

	// Wait for the system to finish booting.
	d.WaitForLogMessage("usage: k <command>")

	// Force a lockup, and expect a reboot to be the result.  Look for
	// the "welcome to Zircon" message as our indication that the system
	// has rebooted.
	d.RunCommand("k lockup test_critical_section 1 1000")
	d.WaitForLogMessage("welcome to Zircon")

	// TODO(fxbug.dev/81295): Our emulated x64 environment does not currently
	// support crashlogs, however our ARM64 environment does.  If we are on ARM,
	// check the crashlog startup banner to make certain that the system didn't
	// just reboot, but that it did so because of a SOFTWARE WATCHDOG event.
	if arch == emulator.Arm64 {
		d.WaitForLogMessage("SW Reason \"SW WATCHDOG\"")
	}

	// Wait for the system to finish booting (again).
	d.WaitForLogMessage("usage: k <command>")
}

func TestKernelLockupDetectorFatalHeartbeat(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, common_args...)
	device.KernelArgs = append(device.KernelArgs,
		"kernel.lockup-detector.heartbeat-age-fatal-threshold-ms=500")
	d := distro.Create(device)

	// Boot.
	d.Start()

	// Wait for the system to finish booting.
	d.WaitForLogMessage("usage: k <command>")

	// Force a lockup, and expect a reboot to be the result.  Look for
	// the "welcome to Zircon" message as our indication that the system
	// has rebooted.
	d.RunCommand("k lockup test_spinlock 1 1000")
	d.WaitForLogMessage("welcome to Zircon")

	// TODO(fxbug.dev/81295): Our emulated x64 environment does not currently
	// support crashlogs, however our ARM64 environment does.  If we are on ARM,
	// check the crashlog startup banner to make certain that the system didn't
	// just reboot, but that it did so because of a SOFTWARE WATCHDOG event.
	if arch == emulator.Arm64 {
		d.WaitForLogMessage("SW Reason \"SW WATCHDOG\"")
	}

	// Wait for the system to finish booting (again).
	d.WaitForLogMessage("usage: k <command>")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
