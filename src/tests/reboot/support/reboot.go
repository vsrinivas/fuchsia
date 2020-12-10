// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package support

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

func zbiPath(t *testing.T, zbi_name string) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../"+zbi_name)
}

type ExpectedRebootType int

const (
	CleanReboot = iota
	UncleanReboot
)

// RebootWithCommand is a test helper that boots a qemu instance then reboots it by issuing cmd.
func RebootWithCommand(t *testing.T, cmd string, kind ExpectedRebootType) {
	RebootWithCommandAndZbi(t, cmd, kind, "fuchsia.zbi")
}

func RebootWithCommandAndZbi(t *testing.T, cmd string, kind ExpectedRebootType, zbi_name string) {
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           zbiPath(t, zbi_name),
		AppendCmdline: "devmgr.log-to-debuglog",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial, and wait for fshost to start.
	i.WaitForLogMessages([]string{"console.shell: enabled", "fshost.cm"})

	// Trigger a reboot in one of the various ways.
	i.RunCommand(cmd)

	if kind == CleanReboot {
		// Make sure the file system is notified and unmounts.
		i.WaitForLogMessage("fshost shutdown complete")
	}

	// Is the target rebooting?
	i.WaitForLogMessage("Shutting down debuglog")

	// See that the target comes back up.
	i.WaitForLogMessage("welcome to Zircon")
}
