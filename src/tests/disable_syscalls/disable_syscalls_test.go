package main

import (
	"os"
	"path/filepath"
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

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "kernel.enable-debugging-syscalls=false",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

	// Check status of syscalls by doing a test call, and ensure disabled.
	i.RunCommand("/boot/bin/syscall-check")
	i.WaitForLogMessage("zx_process_write_memory: disabled")
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

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "kernel.enable-debugging-syscalls=true",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

	// Check status of syscalls by doing a test call, and ensure enabled.
	i.RunCommand("/boot/bin/syscall-check")
	i.WaitForLogMessage("zx_process_write_memory: enabled")
}
