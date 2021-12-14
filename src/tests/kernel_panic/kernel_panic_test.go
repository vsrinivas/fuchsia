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

var cmdline = []string{
	"kernel.halt-on-panic=true",
	"kernel.bypass-debuglog=true",
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

// See that `k crash` crashes the kernel.
func TestBasicCrash(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel.
	i.RunCommand("k crash")

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("{{{bt:0:")
}

// See that reading a userspace page from the kernel is fatal.
func TestReadUserMemoryViolation(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	if arch != emulator.X64 {
		// TODO(fxbug.dev/59284): Enable this test once we have PAN support.
		t.Skip("Skipping test. This test only supports x64 targets.")
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel by causing a userspace data read.
	i.RunCommand("k crash_user_read")

	// See that an SMAP failure was identified and that the kernel panicked.
	i.WaitForLogMessageAssertNotSeen("SMAP failure", "cpu does not support smap; will not crash")
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("{{{bt:0:")
}

// See that executing a userspace page from the kernel is fatal.
func TestExecuteUserMemoryViolation(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel by causing a userspace code execution.
	i.RunCommand("k crash_user_execute")

	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	if arch == emulator.X64 {
		i.WaitForLogMessage("page fault in kernel mode")
	} else {
		i.WaitForLogMessage("instruction abort in kernel mode")
	}
	i.WaitForLogMessage("{{{bt:0:")
}

// See that the pmm checker can detect pmm free list corruption.
//
// Verify both oops and panic actions.
func TestPmmCheckerOopsAndPanic(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	if arch != emulator.X64 {
		t.Skip("Skipping test. This test only supports x64 targets.")
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// This test is incompatible with Address Sanitizer.
	i.RunCommand("k build_instrumentation")
	const kasan = "build_instrumentation: address_sanitizer"
	if match := i.WaitForAnyLogMessage(kasan, "build_instrumentation: done"); match == kasan {
		t.Skipf("Skipping test. This test is incompatible with Address Sanitizer")
	}

	// Enable the pmm checker with action oops.
	i.RunCommand("k pmm checker enable 4096 oops")
	i.WaitForLogMessage("pmm checker enabled")

	// Corrupt the free list.
	i.RunCommand("k crash_pmm_use_after_free")
	i.WaitForLogMessage("crash_pmm_use_after_free done")

	// Force a check.
	i.RunCommand("k pmm checker check")

	// See that the corruption is detected and triggered an oops.
	i.WaitForLogMessage("ZIRCON KERNEL OOPS")
	i.WaitForLogMessage("pmm checker found unexpected pattern in page at")
	i.WaitForLogMessage("dump of page follows")
	i.WaitForLogMessage("done")

	// Re-enable with action panic.
	i.RunCommand("k pmm checker enable 4096 panic")
	i.WaitForLogMessage("pmm checker enabled")

	// Corrupt the free list a second time.
	i.RunCommand("k crash_pmm_use_after_free")
	i.WaitForLogMessage("crash_pmm_use_after_free done")

	// Force a check.
	i.RunCommand("k pmm checker check")

	// See that the corruption is detected, but this time results in a panic.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("pmm checker found unexpected pattern in page at")
	i.WaitForLogMessage("dump of page follows")
}

// See that `k crash_assert` crashes the kernel.
func TestCrashAssert(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel.
	i.RunCommand("k crash_assert")

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")

	// See that it was an assert failure and that the assert message was printed.
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
