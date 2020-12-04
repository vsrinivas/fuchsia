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

const cmdline = "console.shell=true kernel.bypass-debuglog=true zircon.autorun.boot=/boot/bin/sh+-c+k"

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../bringup.zbi")
}

func TestConsoleIsLaunched(t *testing.T) {
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

	// Print a string and check for the string.
	i.RunCommand("echo MY_TEST_STRING")
	i.WaitForLogMessage("MY_TEST_STRING")

	// Check that 'ls' doesn't hang by running it and then another echo.
	i.RunCommand("ls")
	i.RunCommand("echo MY_TEST_STRING2")
	i.WaitForLogMessage("MY_TEST_STRING2")

	// Tell the shell to exit.
	i.RunCommand("exit")

	// Check the exit print
	i.WaitForLogMessage("console-launcher: console shell exited (started=1 exited=1, return_code=0)")

	// Print another string to make sure the shell came back up.
	i.RunCommand("echo MY_TEST_STRING3")

	// See that it was printed.
	i.WaitForLogMessage("MY_TEST_STRING3")

	// Run the permissions test.
	i.RunCommand("runtests -n shell-permissions-test")

	// See that it succeeded.
	i.WaitForLogMessage("[runtests][PASSED]")

}
