// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/qemu"
	"go.fuchsia.dev/fuchsia/src/tests/disable_shell/support"
)

func TestShellEnabled(t *testing.T) {
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
		ZBI:           support.ZbiPath(t),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=true",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("vc: Successfully attached")
	tokenFromSerial := support.RandomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessage(tokenFromSerial)
}

func TestAutorunEnabled(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	tokenFromSerial := support.RandomTokenAsString()
	i := distro.Create(qemu.Params{
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

	// Note that this is actually looking for the log message from driver_manager when it runs the
	// autorun command, because the autorun output doesn't go to serial. We look for
	// "console.shell: enabled" first because cmdargs are also logged early in boot.
	i.WaitForLogMessage("console.shell: enabled")
	i.WaitForLogMessage(tokenFromSerial)
}
