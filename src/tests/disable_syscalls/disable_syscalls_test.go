package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"fuchsia.googlesource.com/testing/qemu"
)

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)

	return filepath.Join(exPath, "../zedboot.zbi")
}

func toolPath(t *testing.T, name string) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "test_data", "tools", name)
}

func ensureContains(t *testing.T, output string, lookFor string) {
	if !strings.Contains(output, lookFor) {
		t.Fatalf("output did not contain '%s'", lookFor)
	}
}

func TestDisableDebuggingSyscalls(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	output, err := distro.RunNonInteractive(
		"/boot/bin/syscall-check",
		toolPath(t, "minfs"),
		toolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           zbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=false",
		})
	if err != nil {
		t.Fatal(err)
	}

	ensureContains(t, output, "zx_debug_read: disabled")
	ensureContains(t, output, "zx_debug_send_command: disabled")
	ensureContains(t, output, "zx_debug_write: disabled")
	ensureContains(t, output, "zx_ktrace_control: disabled")
	ensureContains(t, output, "zx_ktrace_read: disabled")
	ensureContains(t, output, "zx_ktrace_write: disabled")
	ensureContains(t, output, "zx_mtrace_control: disabled")
	ensureContains(t, output, "zx_process_write_memory: disabled")
	ensureContains(t, output, "zx_system_mexec: disabled")
	ensureContains(t, output, "zx_system_mexec_payload_get: disabled")
}

func TestEnableDebuggingSyscalls(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	output, err := distro.RunNonInteractive(
		"/boot/bin/syscall-check",
		toolPath(t, "minfs"),
		toolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           zbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	ensureContains(t, output, "zx_debug_read: enabled")
	ensureContains(t, output, "zx_debug_send_command: enabled")
	ensureContains(t, output, "zx_debug_write: enabled")
	ensureContains(t, output, "zx_ktrace_control: enabled")
	ensureContains(t, output, "zx_ktrace_read: enabled")
	ensureContains(t, output, "zx_ktrace_write: enabled")
	ensureContains(t, output, "zx_mtrace_control: enabled")
	ensureContains(t, output, "zx_process_write_memory: enabled")
	ensureContains(t, output, "zx_system_mexec: enabled")
	ensureContains(t, output, "zx_system_mexec_payload_get: enabled")
}
