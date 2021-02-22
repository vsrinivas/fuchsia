// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

var cmdline = []string{
	"console.shell=true",
	"kernel.bypass-debuglog=true",
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

func TestConsoleIsLaunched(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	// The bringup zbi. See //build/images/bringup/BUILD.gn
	device.Initrd = "fuchsia"
	device.Drive = nil

	i := distro.Create(device)
	i.Start()

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

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
