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

var cmdline = []string{
	"kernel.halt-on-panic=true",
	"kernel.bypass-debuglog=true",
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

// See that `k crash` crashes the kernel.
func TestBasicCrash(t *testing.T) {
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

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Crash the kernel.
	if err = i.RunCommand("k crash"); err != nil {
		t.Fatal(err)
	}

	// See that it panicked.
	if err = i.WaitForLogMessage("ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("{{{bt:0:"); err != nil {
		t.Fatal(err)
	}
}

// See that an SMAP violation is fatal.
func TestSMAPViolation(t *testing.T) {
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
	if arch != emulator.X64 {
		t.Skipf("Skipping test. This test only supports x64 targets.\n")
		return
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

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Crash the kernel by violating SMAP.
	if err = i.RunCommand("k crash_user_read"); err != nil {
		t.Fatal(err)
	}

	// See that an SMAP failure was identified and that the kernel panicked.
	if err = i.WaitForLogMessageAssertNotSeen("SMAP failure", "cpu does not support smap; will not crash"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("{{{bt:0:"); err != nil {
		t.Fatal(err)
	}
}

// See that the pmm checker can detect pmm free list corruption.
//
// Verify both oops and panic actions.
func TestPmmCheckerOopsAndPanic(t *testing.T) {
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
	if arch != emulator.X64 {
		t.Skipf("Skipping test. This test only supports x64 targets.\n")
		return
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

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// This test is incompatible with Address Sanitizer.
	if err = i.RunCommand("k build_instrumentation"); err != nil {
		t.Fatal(err)
	}
	const kasan = "build_instrumentation: address_sanitizer"
	if match, err := i.WaitForAnyLogMessage(kasan, "build_instrumentation: done"); err != nil {
		t.Fatalf("failed to check for address_sanitizer instrumentation: %v", err)
	} else if match == kasan {
		t.Skipf("Skipping test. This test is incompatible with Address Sanitizer")
	}

	// Enable the pmm checker with action oops.
	if err = i.RunCommand("k pmm checker enable 4096 oops"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("pmm checker enabled"); err != nil {
		t.Fatal(err)
	}

	// Corrupt the free list.
	if err = i.RunCommand("k crash_pmm_use_after_free"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("crash_pmm_use_after_free done"); err != nil {
		t.Fatal(err)
	}

	// Force a check.
	if err = i.RunCommand("k pmm checker check"); err != nil {
		t.Fatal(err)
	}

	// See that the corruption is detected and triggered an oops.
	if err = i.WaitForLogMessage("ZIRCON KERNEL OOPS"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("pmm checker found unexpected pattern in page at"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("dump of page follows"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("done"); err != nil {
		t.Fatal(err)
	}

	// Re-enable with action panic.
	if err = i.RunCommand("k pmm checker enable 4096 panic"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("pmm checker enabled"); err != nil {
		t.Fatal(err)
	}

	// Corrupt the free list a second time.
	if err = i.RunCommand("k crash_pmm_use_after_free"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("crash_pmm_use_after_free done"); err != nil {
		t.Fatal(err)
	}

	// Force a check.
	if err = i.RunCommand("k pmm checker check"); err != nil {
		t.Fatal(err)
	}

	// See that the corruption is detected, but this time results in a panic.
	if err = i.WaitForLogMessage("ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("pmm checker found unexpected pattern in page at"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("dump of page follows"); err != nil {
		t.Fatal(err)
	}
}

// See that `k crash_assert` crashes the kernel.
func TestCrashAssert(t *testing.T) {
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

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Crash the kernel.
	if err = i.RunCommand("k crash_assert"); err != nil {
		t.Fatal(err)
	}

	// See that it panicked.
	if err = i.WaitForLogMessage("ZIRCON KERNEL PANIC"); err != nil {
		t.Fatal(err)
	}

	// See that it was an assert failure and that the assert message was printed.
	if err = i.WaitForLogMessage("ASSERT FAILED"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("value 42"); err != nil {
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
