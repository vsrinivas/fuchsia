// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/src/tests/disable_syscalls/support"
)

func TestDisableDebuggingOutputOnlySerialSyscalls(t *testing.T) {
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
	device.Initrd = "zircon-r" // zedboot zbi.
	device.KernelArgs = append(device.KernelArgs, "kernel.enable-debugging-syscalls=false", "kernel.enable-serial-syscalls=output-only")

	stdout, stderr, err := distro.RunNonInteractive(
		"/boot/bin/syscall-check",
		support.ToolPath(t, "minfs"),
		support.ToolPath(t, "zbi"),
		device,
	)
	if err != nil {
		t.Fatal(err)
	}

	support.EnsureContains(t, stdout, "zx_debug_read: disabled")
	support.EnsureContains(t, stdout, "zx_debug_write: enabled")

	support.EnsureContains(t, stdout, "zx_debug_send_command: disabled")
	support.EnsureContains(t, stdout, "zx_ktrace_control: disabled")
	support.EnsureContains(t, stdout, "zx_ktrace_read: disabled")
	support.EnsureContains(t, stdout, "zx_ktrace_write: disabled")
	support.EnsureContains(t, stdout, "zx_mtrace_control: disabled")
	support.EnsureContains(t, stdout, "zx_process_write_memory: disabled")
	support.EnsureContains(t, stdout, "zx_system_mexec: disabled")
	support.EnsureContains(t, stdout, "zx_system_mexec_payload_get: disabled")
	if stderr != "" {
		t.Fatal(stderr)
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
