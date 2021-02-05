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

var cmdline = []string{
	"console.shell=true",
	"kernel.bypass-debuglog=true",
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

func TestConsoleIsLaunched(t *testing.T) {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	// The bringup zbi. See //build/images/bringup/BUILD.gn
	device.Initrd = "fuchsia"
	device.Drive = nil

	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// Wait for the system to finish booting.
	if err = i.WaitForLogMessage("usage: k <command>"); err != nil {
		t.Fatal(err)
	}

	// Print a string and check for the string.
	if err = i.RunCommand("echo MY_TEST_STRING"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("MY_TEST_STRING"); err != nil {
		t.Fatal(err)
	}

	// Check that 'ls' doesn't hang by running it and then another echo.
	if err = i.RunCommand("ls"); err != nil {
		t.Fatal(err)
	}
	if err = i.RunCommand("echo MY_TEST_STRING2"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage("MY_TEST_STRING2"); err != nil {
		t.Fatal(err)
	}

	// Tell the shell to exit.
	if err = i.RunCommand("exit"); err != nil {
		t.Fatal(err)
	}

	// Check the exit print
	if err = i.WaitForLogMessage("console-launcher: console shell exited (started=1 exited=1, return_code=0)"); err != nil {
		t.Fatal(err)
	}

	// Print another string to make sure the shell came back up.
	if err = i.RunCommand("echo MY_TEST_STRING3"); err != nil {
		t.Fatal(err)
	}

	// See that it was printed.
	if err = i.WaitForLogMessage("MY_TEST_STRING3"); err != nil {
		t.Fatal(err)
	}

	// Run the permissions test.
	if err = i.RunCommand("runtests -n shell-permissions-test"); err != nil {
		t.Fatal(err)
	}

	// See that it succeeded.
	if err = i.WaitForLogMessage("[runtests][PASSED]"); err != nil {
		t.Fatal(err)
	}
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
