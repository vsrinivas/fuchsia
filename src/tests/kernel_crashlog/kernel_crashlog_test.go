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

// Boots an instance, |crash_cmd|, waits for the system to reboot, prints the
// recovered crash report.
func testCommon(t *testing.T, crash_cmd string) *emulatortest.Instance {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	if arch != emulator.Arm64 {
		// TODO(maniscalco): Flesh out the qemu/x64 support for stowing/retrieving a
		// crashlog.
		t.Skipf("Skipping test. This test only supports arm64 targets.\n")
		return nil
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.Hw.EnableKvm = false

	// Be sure to reboot instead of halt so the newly booted kernel instance can retrieve
	// the previous instance's crashlog.
	//
	// Upon booting run "k", which will print a usage message.  By waiting for the usage
	// message, we can be sure the system has booted and is ready to accept "k"
	// commands.
	device.KernelArgs = append(device.KernelArgs, "kernel.halt-on-panic=false", "zircon.autorun.boot=/boot/bin/sh+-c+k")

	i := distro.Create(device)

	// Boot.
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel.
	i.RunCommand(crash_cmd)
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")

	// Now that the kernel has panicked, it should reboot.  Wait for it to come back up.
	i.WaitForLogMessage("welcome to Zircon")
	i.WaitForLogMessage("usage: k <command>")

	// Early in boot, the system should have recovered the stowed crashlog and stored it in
	// last-panic.txt.  We're dumping that file using dd instead of cat because dd is part of
	// the system image and cat is not.
	i.RunCommand("dd if=/boot/log/last-panic.txt")

	// See that the crashlog looks reasonable.
	i.WaitForLogMessage("ZIRCON REBOOT REASON (KERNEL PANIC)")
	return i
}

// See that the kernel stows a crashlog upon panicking.
func TestKernelCrashlog(t *testing.T) {
	i := testCommon(t, "k crash")
	// See that the crash report contains ESR and FAR.
	//
	// This is a regression test for fxbug.dev/52182.
	i.WaitForLogMessage("esr:         0x96000045")
	i.WaitForLogMessage("far:                0x1")

	// And a backtrace and counters.
	i.WaitForLogMessage("BACKTRACE")
	i.WaitForLogMessage("{{{bt:0")
	i.WaitForLogMessage("counters: ")
}

// See that when the kernel crashes because of an assert failure the crashlog contains the assert
// message.
func TestKernelCrashlogAssert(t *testing.T) {
	i := testCommon(t, "k crash_assert")
	// See that there's a backtrace, followed by some counters, and finally the assert
	// message.
	i.WaitForLogMessage("BACKTRACE")
	i.WaitForLogMessage("{{{bt:0")
	i.WaitForLogMessage("counters: ")
	i.WaitForLogMessage("panic buffer: ")
	i.WaitForLogMessage("KERNEL PANIC")
	i.WaitForLogMessage("ASSERT FAILED")
	i.WaitForLogMessage("value 42")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
