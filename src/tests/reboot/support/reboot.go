// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package support

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

type ExpectedRebootType int

const (
	CleanReboot = iota
	UncleanReboot
)

// RebootWithCommand is a test helper that boots a qemu instance then reboots it by issuing cmd.
func RebootWithCommand(t *testing.T, cmd string, kind ExpectedRebootType) {
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

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

	// Trigger a reboot in one of the various ways.
	i.RunCommand(cmd)

	if kind == CleanReboot {
		// Make sure the file system is notified and unmounts.
		i.WaitForLogMessage("fshost: shutdown complete")
	}

	// Is the target rebooting?
	i.WaitForLogMessage("Shutting down debuglog")

	// See that the target comes back up.
	i.WaitForLogMessage("welcome to Zircon")
}
