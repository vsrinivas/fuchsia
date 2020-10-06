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

	d := distro.Create(qemu.Params{
		Arch: arch,
		ZBI:  zbiPath(t),

		// Enable the lockup detector.
		//
		// Upon booting run "k", which will print a usage message.  By waiting for the usage
		// message, we can be sure the system has booted and is ready to accept "k"
		// commands.
		AppendCmdline: "kernel.lockup-detector.threshold-ms=500 " +
			"zircon.autorun.boot=/boot/bin/sh+-c+k",
	})

	// Boot.
	d.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer d.Kill()

	// Wait for the system to finish booting.
	d.WaitForLogMessage("usage: k <command>")

	// Force two lockups and see that an OOPS is emitted for each one.
	//
	// Why force two lockups?  Because emitting an OOPS will call back into the lockup detector,
	// we want to verify that doing so does not mess up the lockup detector's state and prevent
	// subsequent events from being detected.
	for i := 0; i < 2; i++ {
		d.RunCommand("k lockup test 1 600")
		d.WaitForLogMessage("locking up CPU")
		d.WaitForLogMessage("ZIRCON KERNEL OOPS")
		d.WaitForLogMessage("CPU-1 in critical section for")
		d.WaitForLogMessage("done")
	}
}
