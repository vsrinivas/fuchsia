// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/qemu"
)

const cmdline = "kernel.halt-on-panic=true kernel.bypass-debuglog=true zircon.autorun.boot=/boot/bin/sh+-c+k"

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

// See that `k crash` crashes the kernel.
func TestBasicCrash(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: cmdline,
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel.
	i.RunCommand("k crash")

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("{{{bt:0:")
}

// See that an SMAP violation is fatal.
func TestSMAPViolation(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}
	if arch != qemu.X64 {
		t.Skipf("Skipping test. This test only supports x64 targets.\n")
		return
	}

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: cmdline,
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// Crash the kernel by violating SMAP.
	i.RunCommand("k crash_user_read")

	// See that an SMAP failure was identified and that the kernel panicked.
	i.WaitForLogMessage("SMAP failure")
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("{{{bt:0:")
}

// See that the pmm checker can detect pmm free list corruption.
//
// Verify both oops and panic actions.
func TestPmmCheckerOopsAndPanic(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}
	if arch != qemu.X64 {
		t.Skipf("Skipping test. This test only supports x64 targets.\n")
		return
	}

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: cmdline,
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

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
