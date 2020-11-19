// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/src/tests/disable_syscalls/support"
)

func TestDisabledMakesKtraceFail(t *testing.T) {
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	stdout, stderr, err := distro.RunNonInteractive(
		"/boot/bin/ktrace start 0xff",
		support.ToolPath(t, "minfs"),
		support.ToolPath(t, "zbi"),
		emulator.Params{
			Arch:          arch,
			ZBI:           support.ZbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=false",
		})
	if err != nil {
		t.Fatal(err)
	}

	if stdout != "" {
		t.Fatal(stdout)
	}
	support.EnsureContains(t, stderr, "ZX_ERR_NOT_SUPPORTED")
}
