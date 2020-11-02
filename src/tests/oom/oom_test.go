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

const cmdline = "devmgr.log-to-debuglog kernel.oom.behavior=reboot"

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

// Leaks memory until an out of memory event is triggered, then backs off.  Verifies that the system
// reboots in a somewhat orderly fashion.
func TestOOM(t *testing.T) {
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

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger a simulated OOM.
	i.RunCommand("k pmm oom")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// Make sure the file system is notified and unmounts.
	i.WaitForLogMessage("Successfully waited for VFS exit completion")

	// Ensure the OOM thread reboots the target.
	i.WaitForLogMessage("memory-pressure: rebooting due to OOM")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("memory-pressure: stowing crashlog")
	i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "ZIRCON KERNEL PANIC")
}

// Similar to |TestOOM| this test will trigger an out of memory situation and verify the system
// reboots.  It differs from |TestOOM| in that once the out of memory condition is reached, the
// kernel continues to leak memory as fast as it can, which may cause various user mode programs to
// be terminated (e.g. because a page fault cannot commit).  As a result, the reboot sequence may be
// less orderly and predictable.
func TestOOMHard(t *testing.T) {
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

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Trigger an OOM.
	i.RunCommand("k pmm oom hard")
	i.WaitForLogMessage("memory-pressure: memory availability state - OutOfMemory")

	// Ensure the OOM thread reboots the target.
	i.WaitForLogMessage("memory-pressure: rebooting due to OOM")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("memory-pressure: stowing crashlog")
	i.WaitForLogMessage("ZIRCON REBOOT REASON (OOM)")

	// Ensure that the system reboots without panicking.
	i.WaitForLogMessageAssertNotSeen("welcome to Zircon", "ZIRCON KERNEL PANIC")
}
