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

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

type specific func(*qemu.Instance)

// Boots an instance, |crash_cmd|, waits for the system to reboot, prints the recovered crash report
// and calls |s| to match against test case specific output.
func testCommon(t *testing.T, crash_cmd string, s specific) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}
	if arch != qemu.Arm64 {
		// TODO(maniscalco): Flesh out the qemu/x64 support for stowing/retrieving a
		// crashlog.
		t.Skipf("Skipping test. This test only supports arm64 targets.\n")
		return
	}

	i := distro.Create(qemu.Params{
		Arch: arch,
		ZBI:  zbiPath(t),

		// Be sure to reboot intead of halt so the newly booted kernel instance can retrieve
		// the previous instance's crashlog.
		//
		// Upon booting run "k", which will print a usage message.  By waiting for the usage
		// message, we can be sure the system has booted and is ready to accept "k"
		// commands.
		AppendCmdline: "kernel.halt-on-panic=false zircon.autorun.boot=/boot/bin/sh+-c+k",
	})

	// Boot.
	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

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

	s(i)
}

// See that the kernel stows a crashlog upon panicking.
func TestKernelCrashlog(t *testing.T) {
	testCommon(t, "k crash", func(i *qemu.Instance) {
		// See that the crash report contains ESR and FAR.
		//
		// This is a regression test for fxbug.dev/52182.
		i.WaitForLogMessage("esr:         0x96000045")
		i.WaitForLogMessage("far:                0x1")

		// And a backtrace and counters.
		i.WaitForLogMessage("BACKTRACE")
		i.WaitForLogMessage("{{{bt:0")
		i.WaitForLogMessage("counters: ")
	})
}

// See that when the kernel crashes because of an assert failure the crashlog contains the assert
// message.
func TestKernelCrashlogAssert(t *testing.T) {
	testCommon(t, "k crash_assert", func(i *qemu.Instance) {
		// See that there's a backtrace, followed by some counters, and finally the assert
		// message.
		i.WaitForLogMessage("BACKTRACE")
		i.WaitForLogMessage("{{{bt:0")
		i.WaitForLogMessage("counters: ")
		i.WaitForLogMessage("panic buffer: ")
		i.WaitForLogMessage("KERNEL PANIC")
		i.WaitForLogMessage("ASSERT FAILED")
		i.WaitForLogMessage("value 42")
	})
}
