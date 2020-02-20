package main

import (
	"testing"

	"fuchsia.googlesource.com/testing/qemu"
	"fuchsia.googlesource.com/tests/disable_syscalls/support"
)

func TestDisableDebuggingEnableSerialSyscalls(t *testing.T) {
	distro, err := qemu.Unpack()
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
		qemu.Params{
			Arch:          arch,
			ZBI:           support.ZbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=false kernel.enable-serial-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	support.EnsureContains(t, stdout, "zx_debug_read: enabled")
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
