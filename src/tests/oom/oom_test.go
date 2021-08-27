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

var cmdline = []string{"devmgr.log-to-debuglog", "kernel.oom.behavior=reboot"}

// Triggers the OOM signal without leaking memory. Verifies that fileystems are shut down and the
// system reboots in a somewhat orderly fashion.
func TestOOMSignal(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger a simulated OOM, without leaking any memory.
	i.RunCommand("k pmm oom signal")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// Make sure the file system is notified and unmounts.
	i.WaitForLogMessage("Successfully waited for VFS exit completion")

	// Ensure the OOM thread reboots the target.
	i.WaitForLogMessage("memory-pressure: rebooting due to OOM")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("memory-pressure: stowing crashlog")
	i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

// Verifies that once the system has committed to an OOM reboot, the termination of a critical
// process will not supercede the OOM and change the reboot reason.
func TestOOMSignalBeforeCriticalProcess(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger a simulated OOM, without leaking any memory.
	i.RunCommand("k pmm oom signal")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// See that we're committed to the OOM.
	i.WaitForLogMessage("memory-pressure: pausing for")

	// Kill a critical process.
	i.RunCommand("killall bootsvc")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("memory-pressure: stowing crashlog")
	i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

// Leaks memory until an out of memory event is triggered, then backs off.  Verifies that the system
// reboots.
func TestOOM(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger an OOM.
	i.RunCommand("k pmm oom")

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
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

// Similar to |TestOOM| this test will trigger an out of memory situation and verify the system
// reboots.  It differs from |TestOOM| in that once the out of memory condition is reached, the
// kernel continues to leak memory as fast as it can, which may cause various user mode programs to
// be terminated (e.g. because a page fault cannot commit).  As a result, the reboot sequence may be
// less orderly and predictable.
func TestOOMHard(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger an OOM.
	i.RunCommand("k pmm oom hard")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
