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
		AppendCmdline: "devmgr.log-to-debuglog",
	})

	err = i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("memory-pressure: memory availability state - Normal")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

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
}
