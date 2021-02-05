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

func TestKernelLockupDetectorCriticalSection(t *testing.T) {
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

	// Enable the lockup detector.
	//
	// Upon booting run "k", which will print a usage message.  By waiting for the usage
	// message, we can be sure the system has booted and is ready to accept "k"
	// commands.
	device.KernelArgs = append(device.KernelArgs, "kernel.lockup-detector.critical-section-threshold-ms=500", "zircon.autorun.boot=/boot/bin/sh+-c+k")
	d, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	// Boot.
	if err = d.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = d.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// Wait for the system to finish booting.
	if err = d.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Force two lockups and see that an OOPS is emitted for each one.
	//
	// Why force two lockups?  Because emitting an OOPS will call back into the lockup detector,
	// we want to verify that doing so does not mess up the lockup detector's state and prevent
	// subsequent events from being detected.
	for i := 0; i < 2; i++ {
		if err = d.RunCommand("k lockup test 1 600"); err != nil {
			t.Fatal(err)
		}
		if err = d.WaitForLogMessage("locking up CPU"); err != nil {
			t.Fatal(err)
		}
		if err = d.WaitForLogMessage("ZIRCON KERNEL OOPS"); err != nil {
			t.Fatal(err)
		}
		if err = d.WaitForLogMessage("CPU-1 in critical section for"); err != nil {
			t.Fatal(err)
		}
		if err = d.WaitForLogMessage("done"); err != nil {
			t.Fatal(err)
		}
	}
}

func TestKernelLockupDetectorHeartbeat(t *testing.T) {
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
	device.KernelArgs = append(device.KernelArgs,
		// Enable the lockup detector.
		//
		// Upon booting run "k", which will print a usage message.  By waiting for the usage
		// message, we can be sure the system has booted and is ready to accept "k"
		// commands.
		"kernel.lockup-detector.heartbeat-period-ms=50",
		"kernel.lockup-detector.heartbeat-age-threshold-ms=200",
		"zircon.autorun.boot=/boot/bin/sh+-c+k",
	)
	d, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	// Boot.
	if err = d.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = d.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// Wait for the system to finish booting.
	if err = d.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Force a lockup and see that a heartbeat OOPS is emitted.
	if err = d.RunCommand("k lockup test 1 1000"); err != nil {
		t.Fatal(err)
	}
	if err = d.WaitForLogMessage("locking up CPU"); err != nil {
		t.Fatal(err)
	}
	if err = d.WaitForLogMessage("ZIRCON KERNEL OOPS"); err != nil {
		t.Fatal(err)
	}
	if err = d.WaitForLogMessage("no heartbeat from CPU-1"); err != nil {
		t.Fatal(err)
	}
	// See that the CPU's run queue is printed and contains the thread named "lockup-spin", the
	// one responsible for the lockup.
	if err = d.WaitForLogMessage("lockup-spin"); err != nil {
		t.Fatal(err)
	}
	if err = d.WaitForLogMessage("done"); err != nil {
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
