// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

var cmdlineCommon = []string{"kernel.oom.behavior=reboot", "kernel.oom.reboot-timeout-ms=8000"}
var initMessages []string = []string{
	// Ensure the kernel OOM system was properly initialized.
	"memory-pressure: memory availability state - Normal",
	"pwrbtn-monitor: OOM monitoring active",
	// Make sure the shell is ready to accept commands over serial.
	"console.shell: enabled",
	"fshost: lifecycle handler ready",
	"Drivers loaded and published",
	"driver_manager loader loop started",
	"driver_manager main loop is running",
	"archivist: Entering core loop"}

// Triggers the OOM signal without leaking memory. Verifies that filesystems are shut down and the
// system reboots in a somewhat orderly fashion.
func TestOOMSignal(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdlineCommon...)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	initMsgs := make([]string, len(initMessages))
	copy(initMsgs, initMessages)
	// Make sure some of the basic systems are initialized
	i.WaitForLogMessages(initMsgs)

	// Trigger a simulated OOM, without leaking any memory.
	i.RunCommand("k pmm oom signal")

	// Sometimes fshost shut down so quickly, its messages are printed
	// before the memory-pressure ones.
	i.WaitForLogMessages([]string{"memory-pressure: memory availability state - OutOfMemory",
		"received shutdown command over lifecycle interface",
		"fshost shutdown complete"})

	// Ensure the OOM thread reboots the target.
	i.WaitForLogMessage("memory-pressure: rebooting due to OOM. received user-mode acknowledgement.")

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
	device.KernelArgs = append(device.KernelArgs, cmdlineCommon...)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	initMsgs := make([]string, len(initMessages))
	copy(initMsgs, initMessages)
	// Make sure some of the basic systems are initialized
	i.WaitForLogMessages(initMsgs)

	// Trigger a simulated OOM, without leaking any memory.
	i.RunCommand("k pmm oom signal")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// See that we're committed to the OOM.
	i.WaitForLogMessage("memory-pressure: pausing for")

	// Kill a critical process.
	i.RunCommand("killall bin/component_manager")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("memory-pressure: stowing crashlog")
	i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

// Boots with |cmdline| appended to boot-options, runs |cmd|, then waits for any of |msgs| to be
// emitted.
func testOOMCommon(t *testing.T, cmdline []string, cmd string, msgs ...string) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	initMsgs := make([]string, len(initMessages))
	copy(initMsgs, initMessages)
	// Make sure some of the basic systems are initialized
	i.WaitForLogMessages(initMsgs)

	// Trigger an OOM.
	i.RunCommand(cmd)

	i.WaitForAnyLogMessage(msgs...)

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "KERNEL PANIC")
}

var stateTransitionString string = "memory-pressure: memory availability state - OutOfMemory"

// Leaks memory until an out of memory event is triggered, then backs off.  Verifies that the system
// reboots.
func TestOOM(t *testing.T) {
	// Ensure the memory availability state transition happens.
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
	testOOMCommon(t, cmdlineCommon, "k pmm oom", stateTransitionString)
}

// Similar to |TestOOM| this test will trigger an out of memory situation and verify the system
// reboots.  It differs from |TestOOM| in that once the out of memory condition is reached, the
// kernel continues to leak memory as fast as it can, which may cause various user mode programs to
// be terminated (e.g. because a page fault cannot commit).  As a result, the reboot sequence may be
// less orderly and predictable.
func TestOOMHard(t *testing.T) {
	// This command will keep on trying to allocate even after all memory is exhausted (and
	// allocations have failed).
	testOOMCommon(t, cmdlineCommon, "k pmm oom hard", stateTransitionString)
}

// See that failing to allocate will trigger an OOM reboot.
func TestOOMDip(t *testing.T) {
	testOOMCommon(t, cmdlineCommon, "k pmm oom dip", stateTransitionString)
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
