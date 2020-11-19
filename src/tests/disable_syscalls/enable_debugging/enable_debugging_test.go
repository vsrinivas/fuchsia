// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/src/tests/disable_syscalls/support"
)

func TestEnableDebuggingSyscalls(t *testing.T) {
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
		"/boot/bin/syscall-check",
		support.ToolPath(t, "minfs"),
		support.ToolPath(t, "zbi"),
		emulator.Params{
			Arch:          arch,
			ZBI:           support.ZbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=true kernel.enable-serial-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	support.EnsureContains(t, stdout, "zx_debug_read: enabled")
	support.EnsureContains(t, stdout, "zx_debug_send_command: enabled")
	support.EnsureContains(t, stdout, "zx_debug_write: enabled")
	support.EnsureContains(t, stdout, "zx_ktrace_control: enabled")
	support.EnsureContains(t, stdout, "zx_ktrace_read: enabled")
	support.EnsureContains(t, stdout, "zx_ktrace_write: enabled")
	support.EnsureContains(t, stdout, "zx_mtrace_control: enabled")
	support.EnsureContains(t, stdout, "zx_process_write_memory: enabled")
	support.EnsureContains(t, stdout, "zx_system_mexec: enabled")
	support.EnsureContains(t, stdout, "zx_system_mexec_payload_get: enabled")
	if stderr != "" {
		t.Fatal(stderr)
	}
}
