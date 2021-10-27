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
	// Use a huge value to make sure we don't time out.
	"kernel.lockup-detector.diagnostic-query-timeout-ms=1000000000",
	// Upon booting run "k", which will print a usage message.  By waiting for the usage
	// message, we can be sure the system has booted and is ready to accept "k" commands.
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

func testCommon(t *testing.T, kernel_arg string) (*emulatortest.Instance, emulator.Arch) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	device.KernelArgs = append(device.KernelArgs, common_args...)
	// Enable the lockup detector using the specified kernel_arg.
	device.KernelArgs = append(device.KernelArgs, kernel_arg)
	i := distro.Create(device)

	// Boot.
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")
	return i, arch
}

func TestKernelLockupDetectorCriticalSection(t *testing.T) {
	i, _ := testCommon(t, "kernel.lockup-detector.critical-section-threshold-ms=200")

	// Force a lockup and see that an OOPS is emitted.
	i.RunCommand("k lockup test_critical_section 1 600")
	i.WaitForLogMessage("locking up CPU")
	i.WaitForLogMessage("ZIRCON KERNEL OOPS")
	i.WaitForLogMessage("CPU-1 in critical section for")
	i.WaitForLogMessage("done")
}

func TestKernelLockupDetectorHeartbeat(t *testing.T) {
	i, _ := testCommon(t, "kernel.lockup-detector.heartbeat-age-threshold-ms=200")

	// Force a lockup and see that a heartbeat OOPS is emitted.
	i.RunCommand("k lockup test_spinlock 1 1000")
	i.WaitForLogMessage("locking up CPU")
	i.WaitForLogMessage("ZIRCON KERNEL OOPS")
	i.WaitForLogMessage("no heartbeat from CPU-1")
	// See that the CPU's run queue is printed and contains the thread named "lockup-test", the
	// one responsible for the lockup.
	i.WaitForLogMessage("lockup-test")
	i.WaitForLogMessage("done")
}

func testLockupWithCommand(t *testing.T, i *emulatortest.Instance, arch emulator.Arch, command string) {
	// Force a lockup using the given command, and expect a reboot to be the
	// result.  First look for evidence that the lockedup detector printed
	// the context of the unresponsive CPU.  Then look for the "welcome to
	// Zircon" message as our indication that the system has rebooted.
	i.RunCommand(command)
	i.WaitForLogMessage("context follows")
	i.WaitForLogMessage("{{{bt:0:")
	i.WaitForLogMessage("welcome to Zircon")

	// TODO(fxbug.dev/81295): Our emulated x64 environment does not currently
	// support crashlogs, however our ARM64 environment does.  If we are on ARM,
	// check the crashlog startup banner to make certain that the system didn't
	// just reboot, but that it did so because of a SOFTWARE WATCHDOG event.
	if arch == emulator.Arm64 {
		i.WaitForLogMessage("SW Reason \"SW WATCHDOG\"")
	}

	// Wait for the system to finish booting (again).
	i.WaitForLogMessage("usage: k <command>")

}

func TestKernelLockupDetectorFatalCriticalSection(t *testing.T) {
	i, arch := testCommon(t, "kernel.lockup-detector.critical-section-fatal-threshold-ms=500")
	testLockupWithCommand(t, i, arch, "k lockup test_critical_section 1 1000")
}

func TestKernelLockupDetectorFatalHeartbeat(t *testing.T) {
	i, arch := testCommon(t, "kernel.lockup-detector.heartbeat-age-fatal-threshold-ms=500")
	testLockupWithCommand(t, i, arch, "k lockup test_spinlock 1 1000")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
