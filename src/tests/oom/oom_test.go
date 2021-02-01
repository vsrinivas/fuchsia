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

var cmdline = []string{"devmgr.log-to-debuglog", "kernel.oom.behavior=reboot"}

// Triggers the OOM signal without leaking memory. Verifies that fileystems are shut down and the
// system reboots in a somewhat orderly fashion.
func TestOOMSignal(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	}()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
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

	// Ensure the kernel OOM system was properly initialized.
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - Normal"); err != nil {
		t.Fatal(err)
	}

	// Make sure the shell is ready to accept commands over serial.
	if err = i.WaitForLogMessage("console.shell: enabled"); err != nil {
		t.Fatal(err)
	}

	// Trigger a simulated OOM, without leaking any memory.
	if err = i.RunCommand("k pmm oom signal"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory"); err != nil {
		t.Fatal(err)
	}

	// Make sure the file system is notified and unmounts.
	if err = i.WaitForLogMessage("Successfully waited for VFS exit completion"); err != nil {
		t.Fatal(err)
	}

	// Ensure the OOM thread reboots the target.
	if err = i.WaitForLogMessage("memory-pressure: rebooting due to OOM"); err != nil {
		t.Fatal(err)
	}

	// Ensure that the reboot has stowed a correct crashlog.
	if err = i.WaitForLogMessage("memory-pressure: stowing crashlog"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)"); err != nil {
		t.Fatal(err)
	}

	// Ensure that the system reboots without panicking.
	if err = i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
}

// Leaks memory until an out of memory event is triggered, then backs off.  Verifies that the system
// reboots.
func TestOOM(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	}()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
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

	// Ensure the kernel OOM system was properly initialized.
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - Normal"); err != nil {
		t.Fatal(err)
	}

	// Make sure the shell is ready to accept commands over serial.
	if err = i.WaitForLogMessage("console.shell: enabled"); err != nil {
		t.Fatal(err)
	}

	// Trigger an OOM.
	if err = i.RunCommand("k pmm oom"); err != nil {
		t.Fatal(err)
	}

	// Ensure the memory state transition happens.
	//
	// Typically this leads to file systems being shut down and the system rebooting with reason
	// "ZIRCON REBOOT REASON (OOM)".  However, after triggering the OOM signal, we wait for a
	// few seconds before rebooting, which is ample time for unrelated things to go wrong in the
	// OOM-constrained memory environment. For example, critical processes (like driver_manager)
	// could crash when they fail to allocate any more memory, and cause a reboot. So even
	// though the expected reboot reason is an OOM, the system could reboot for completely
	// different reasons.  Due to this unpredictability, we do not check the logs here for the
	// expected sequence of events beyond this point, to prevent the test from flaking. Instead,
	// we have a separate test |TestOOMSignal| to verify that a simulated OOM signal, i.e. an
	// OOM signal without actually leaking any memory, results in the expected sequence of
	// events.
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory"); err != nil {
		t.Fatal(err)
	}

	// Ensure that the system reboots without panicking.
	if err = i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
}

// Similar to |TestOOM| this test will trigger an out of memory situation and verify the system
// reboots.  It differs from |TestOOM| in that once the out of memory condition is reached, the
// kernel continues to leak memory as fast as it can, which may cause various user mode programs to
// be terminated (e.g. because a page fault cannot commit).  As a result, the reboot sequence may be
// less orderly and predictable.
func TestOOMHard(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	}()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
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

	// Ensure the kernel OOM system was properly initialized.
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - Normal"); err != nil {
		t.Fatal(err)
	}

	// Make sure the shell is ready to accept commands over serial.
	if err = i.WaitForLogMessage("console.shell: enabled"); err != nil {
		t.Fatal(err)
	}

	// Trigger an OOM.
	if err = i.RunCommand("k pmm oom hard"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory"); err != nil {
		t.Fatal(err)
	}

	// Ensure that the system reboots without panicking.
	if err = i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "ZIRCON KERNEL PANIC"); err != nil {
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
