// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/src/tests/disable_shell/support"
)

func TestShellDisabled(t *testing.T) {
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
		AppendCmdline: "devmgr.log-to-debuglog console.shell=false",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("console-launcher: running")
	tokenFromSerial := support.RandomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.AssertLogMessageNotSeenWithinTimeout(tokenFromSerial, 3*time.Second)
}

func TestAutorunDisabled(t *testing.T) {
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
		AppendCmdline: "devmgr.log-to-debuglog console.shell=false zircon.autorun.boot=foobar",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("Couldn't launch autorun command 'foobar'")
}
