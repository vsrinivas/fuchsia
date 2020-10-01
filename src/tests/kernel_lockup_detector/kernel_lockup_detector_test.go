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

func TestKernelLockupDetector(t *testing.T) {
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
		Arch: arch,
		ZBI:  zbiPath(t),

		// Enable the lockup detector.
		//
		// Upon booting run "k", which will print a usage message.  By waiting for the usage
		// message, we can be sure the system has booted and is ready to accept "k"
		// commands.
		AppendCmdline: "kernel.lockup-detector.threshold-ms=1000 " +
			"zircon.autorun.boot=/boot/bin/sh+-c+k",
	})

	// Boot.
	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")

	// For a lockup and see that an OOPS is emitted.
	i.RunCommand("k lockup test 1 1100")
	i.WaitForLogMessage("locking up CPU")
	i.WaitForLogMessage("ZIRCON KERNEL OOPS")
	i.WaitForLogMessage("CPU-1 in critical section for")
	i.WaitForLogMessage("done")
}
