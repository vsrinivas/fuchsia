// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

// Boots an instance, |crash_cmd|, waits for the system to reboot, prints the
// recovered crash report.
//
// |crash_cmd|                : The command to execute on the kernel command
//                              line to trigger the crash.
// |expected_crash_indicator| : The string to wait for which indicates that the
//                              system in crashing in the expected way in
//                              response to |crash_cmd|.
// |expected_reboot_reason|   : The string we expect to see in the recovered
//                              crashlog which reports the expected reason for
//                              the crash/reboot.
func testCommon(t *testing.T,
	crash_cmd string,
	expected_crash_indicator string,
	expected_reboot_reason string) *emulatortest.Instance {
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
	device.KernelArgs = append(device.KernelArgs,
		"kernel.halt-on-panic=false",
		"kernel.render-dlog-to-crashlog=true",
		"zircon.autorun.boot=/boot/bin/sh+-c+k")

	i := distro.Create(device)

	// Boot.
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel.
	i.RunCommand(crash_cmd)
	i.WaitForLogMessage(expected_crash_indicator)

	// Now that the kernel has panicked, it should reboot.  Wait for it to come back up.
	i.WaitForLogMessage("welcome to Zircon")
	i.WaitForLogMessage("usage: k <command>")

	// Early in boot, the system should have recovered the stowed crashlog and stored it in
	// last-panic.txt.  We're dumping that file using dd instead of cat because dd is part of
	// the system image and cat is not.
	i.RunCommand("dd if=/boot/log/last-panic.txt")

	// See that the crashlog looks reasonable.
	i.WaitForLogMessage(fmt.Sprintf("ZIRCON REBOOT REASON (%s)", expected_reboot_reason))
	return i
}

// See that the kernel stows a crashlog upon panicking.
func TestKernelCrashlog(t *testing.T) {
	i := testCommon(t, "k crash", "ZIRCON KERNEL PANIC", "KERNEL PANIC")
	// See that the crash report contains ESR and FAR.
	//
	// This is a regression test for fxbug.dev/52182. 'k crash' is going to
	// attempt to store a value at a bad address in order to trigger its
	// exception.  While we cannot always rely on the lower bits of the ESR being
	// consistent, we should be able to rely on the top byte being consistent in
	// this case.  The breakdown of the top bits is:
	//
	// 1) [31:26] EC : 0b100101 => Data Abort taken without a change in Exception level.
	// 2) [25] IL    : 0b1      => Instruction was not a 16 bit instruction
	// 3) [24] ISV   : 0b0      => Should always be 0, for this EC, the ARM ARM states "ISV is 0 for
	//                             all faults reported in ESR_EL1 or ESR_EL3."
	i.WaitForLogMessage("esr:         0x96")
	i.WaitForLogMessage("far:                0x1")

	// And a backtrace and counters.
	i.WaitForLogMessage("BACKTRACE")
	i.WaitForLogMessage("{{{bt:0")
	i.WaitForLogMessage("counters: ")
	i.WaitForLogMessage("--- BEGIN DLOG DUMP ---")
	i.WaitForLogMessage("stopping other cpus")
	i.WaitForLogMessage("--- END DLOG DUMP ---")
}

// See that when the kernel crashes because of an assert failure the crashlog contains the assert
// message.
func TestKernelCrashlogAssert(t *testing.T) {
	i := testCommon(t, "k crash_assert", "ZIRCON KERNEL PANIC", "KERNEL PANIC")
	// See that there's a backtrace, followed by some counters, and finally the assert
	// message.
	i.WaitForLogMessage("BACKTRACE")
	i.WaitForLogMessage("{{{bt:0")
	i.WaitForLogMessage("counters: ")
	i.WaitForLogMessage("panic buffer: ")
	i.WaitForLogMessage("KERNEL PANIC")
	i.WaitForLogMessage("ASSERT FAILED")
	i.WaitForLogMessage("value 42")
	i.WaitForLogMessage("--- BEGIN DLOG DUMP ---")
	i.WaitForLogMessage("stopping other cpus")
	i.WaitForLogMessage("--- END DLOG DUMP ---")
}

func TestKernelCrashlogOom(t *testing.T) {
	testCommon(t, "k pmm oom", "memory-pressure: rebooting due to OOM", "OOM")
}

func TestKernelCrashlogRootJobTermination(t *testing.T) {
	testCommon(t, "killall bootsvc", "root-job: taking reboot action", "USERSPACE ROOT JOB TERMINATION")
}

func TestKernelCrashlogNoCrash(t *testing.T) {
	testCommon(t, "dm reboot", "[shutdown-shim]: started", "NO CRASH")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
