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
	return filepath.Join(exPath, "../fuchsia.zbi")
}

func TestOOM(t *testing.T) {
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
		Arch: arch,
		ZBI:  zbiPath(t),
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	// Ensure the kernel OOM system was properly initialized.
	i.WaitForLogMessage("OOM: memory availability state 3")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("vc: Successfully attached")

	// Trigger a simulated OOM.
	i.RunCommand("k pmm oom")
	i.WaitForLogMessage("OOM: memory availability state 0")

	// Make sure the file system is notified and unmounts.
	i.WaitForLogMessage("driver_manager: Successfully waited for VFS exit completion")

	// Ensure the OOM thread reboots the target.
	i.WaitForLogMessage("OOM: rebooting")

	// Ensure that the reboot has stowed a correct crashlog.
	i.WaitForLogMessage("stowing crashlog")
	i.WaitForLogMessage("ZIRCON OOM")
}
