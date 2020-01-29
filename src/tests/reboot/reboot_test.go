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

func TestReboot(t *testing.T) {
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

	commandList := []string{"dm reboot", "dm reboot-recovery", "dm reboot-bootloader"}

	for _, cmd := range commandList {
		i.WaitForLogMessage("initializing platform")

		// Make sure the shell is ready to accept commands over serial.
		i.WaitForLogMessage("vc: Successfully attached")

		// Trigger a reboot in one of the various ways
		i.RunCommand(cmd)

		// Make sure the file system is notified and unmounts.
		i.WaitForLogMessage("driver_manager: Successfully waited for VFS exit completion")

		// Is the target rebooting?
		i.WaitForLogMessage("Shutting down debuglog")
	}
}
