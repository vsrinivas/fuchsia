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

func ensureDoesNotContain(t *testing.T, output string, lookFor string) {
	if strings.Contains(output, lookFor) {
		t.Fatalf("output contains '%s'", lookFor)
	}
}

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
		toolPath(t, "minfs"),
		toolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           zbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=false kernel.enable-serial-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	ensureContains(t, stdout, "zx_debug_read: enabled")
	ensureContains(t, stdout, "zx_debug_write: enabled")

	ensureContains(t, stdout, "zx_debug_send_command: disabled")
	ensureContains(t, stdout, "zx_ktrace_control: disabled")
	ensureContains(t, stdout, "zx_ktrace_read: disabled")
	ensureContains(t, stdout, "zx_ktrace_write: disabled")
	ensureContains(t, stdout, "zx_mtrace_control: disabled")
	ensureContains(t, stdout, "zx_process_write_memory: disabled")
	ensureContains(t, stdout, "zx_system_mexec: disabled")
	ensureContains(t, stdout, "zx_system_mexec_payload_get: disabled")
	if stderr != "" {
		t.Fatal(stderr)
	}
}

func TestDisableDebuggingDisableSerialSyscalls(t *testing.T) {
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
		toolPath(t, "minfs"),
		toolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           zbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=false kernel.enable-serial-syscalls=false",
		})
	if err != nil {
		t.Fatal(err)
	}

	ensureContains(t, stdout, "zx_debug_read: disabled")
	ensureContains(t, stdout, "zx_debug_write: disabled")

	ensureContains(t, stdout, "zx_debug_send_command: disabled")
	ensureContains(t, stdout, "zx_ktrace_control: disabled")
	ensureContains(t, stdout, "zx_ktrace_read: disabled")
	ensureContains(t, stdout, "zx_ktrace_write: disabled")
	ensureContains(t, stdout, "zx_mtrace_control: disabled")
	ensureContains(t, stdout, "zx_process_write_memory: disabled")
	ensureContains(t, stdout, "zx_system_mexec: disabled")
	ensureContains(t, stdout, "zx_system_mexec_payload_get: disabled")
	if stderr != "" {
		t.Fatal(stderr)
	}
}

func TestDisabledMakesKtraceFail(t *testing.T) {
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
		"/boot/bin/ktrace start 0xff",
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

	if stdout != "" {
		t.Fatal(stdout)
	}
	ensureContains(t, stderr, "ZX_ERR_NOT_SUPPORTED")
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

	stdout, stderr, err := distro.RunNonInteractive(
		"/boot/bin/syscall-check",
		toolPath(t, "minfs"),
		toolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           zbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=true kernel.enable-serial-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	ensureContains(t, stdout, "zx_debug_read: enabled")
	ensureContains(t, stdout, "zx_debug_send_command: enabled")
	ensureContains(t, stdout, "zx_debug_write: enabled")
	ensureContains(t, stdout, "zx_ktrace_control: enabled")
	ensureContains(t, stdout, "zx_ktrace_read: enabled")
	ensureContains(t, stdout, "zx_ktrace_write: enabled")
	ensureContains(t, stdout, "zx_mtrace_control: enabled")
	ensureContains(t, stdout, "zx_process_write_memory: enabled")
	ensureContains(t, stdout, "zx_system_mexec: enabled")
	ensureContains(t, stdout, "zx_system_mexec_payload_get: enabled")
	if stderr != "" {
		t.Fatal(stderr)
	}
}

func TestKtraceWorksWhenEnabled(t *testing.T) {
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
		"/boot/bin/ktrace start 0xff",
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

	ensureDoesNotContain(t, stdout, "ZX_ERR_NOT_SUPPORTED")
	ensureDoesNotContain(t, stderr, "ZX_ERR_NOT_SUPPORTED")
}
