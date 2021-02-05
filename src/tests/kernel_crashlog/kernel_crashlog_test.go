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

// Boots an instance, |crash_cmd|, waits for the system to reboot, prints the
// recovered crash report.
func testCommon(t *testing.T, crash_cmd string) *emulator.Instance {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	})
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}
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

	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	// Boot.
	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	})

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Crash the kernel.
	if err = i.RunCommand(crash_cmd); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}

	// Now that the kernel has panicked, it should reboot.  Wait for it to come back up.
	if err = i.WaitForLogMessage("welcome to Zircon"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Early in boot, the system should have recovered the stowed crashlog and stored it in
	// last-panic.txt.  We're dumping that file using dd instead of cat because dd is part of
	// the system image and cat is not.
	if err = i.RunCommand("dd if=/boot/log/last-panic.txt"); err != nil {
		t.Fatal(err)
	}

	// See that the crashlog looks reasonable.
	if err = i.WaitForLogMessage("ZIRCON REBOOT REASON (KERNEL PANIC)"); err != nil {
		t.Fatal(err)
	}
	return i
}

// See that the kernel stows a crashlog upon panicking.
func TestKernelCrashlog(t *testing.T) {
	i := testCommon(t, "k crash")
	// See that the crash report contains ESR and FAR.
	//
	// This is a regression test for fxbug.dev/52182.
	if err := i.WaitForLogMessage("esr:         0x96000045"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("far:                0x1"); err != nil {
		t.Fatal(err)
	}

	// And a backtrace and counters.
	if err := i.WaitForLogMessage("BACKTRACE"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("{{{bt:0"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("counters: "); err != nil {
		t.Fatal(err)
	}
}

// See that when the kernel crashes because of an assert failure the crashlog contains the assert
// message.
func TestKernelCrashlogAssert(t *testing.T) {
	i := testCommon(t, "k crash_assert")
	// See that there's a backtrace, followed by some counters, and finally the assert
	// message.
	if err := i.WaitForLogMessage("BACKTRACE"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("{{{bt:0"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("counters: "); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("panic buffer: "); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("ASSERT FAILED"); err != nil {
		t.Fatal(err)
	}
	if err := i.WaitForLogMessage("value 42"); err != nil {
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
