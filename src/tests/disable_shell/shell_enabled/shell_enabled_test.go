// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/src/tests/disable_shell/support"
)

func TestShellEnabled(t *testing.T) {
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
		ZBI:           support.ZbiPath(t),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=true",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("console.shell: enabled")
	tokenFromSerial := support.RandomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessage(tokenFromSerial)
}

func TestAutorunEnabled(t *testing.T) {
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	tokenFromSerial := support.RandomTokenAsString()
	i := distro.Create(emulator.Params{
		Arch: arch,
		ZBI:  support.ZbiPath(t),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=true " +
			"zircon.autorun.boot=/boot/bin/sh+-c+echo+" + tokenFromSerial,
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Wait for console-launcher to come up before waiting for the autorun output.
	i.WaitForLogMessage("console-launcher: running")
	i.WaitForLogMessage(tokenFromSerial)
}
