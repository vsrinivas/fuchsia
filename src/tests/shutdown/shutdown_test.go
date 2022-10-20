// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

func TestShutdown(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	// Make sure the shell is ready to accept commands over serial
	// and /dev is mounted for use from the console.
	i.WaitForLogMessages([]string{"console.shell: enabled", "mounted '/dev'"})

	if arch == emulator.X64 {
		// Ensure the ACPI driver comes up before we attempt a shutdown.
		i.RunCommand("waitfor verbose class=acpi topo=/dev/sys/platform/pt/acpi/acpi-_SB_/acpi-_SB_-passthrough && echo ACPI_READY")
		i.WaitForLogMessage("ACPI_READY")
	}

	// Trigger a shutdown.
	i.RunCommand("dm shutdown")

	// Start a timer so we can abort the wait by explicitly killing. This will yield a nice error
	// from the wait command that we can detect.
	timer := time.AfterFunc(120*time.Second, cancel)

	// Cannot check for log messages as we are racing with the shutdown, and if qemu closes first
	// checking for log messages will panic, so we just wait.
	ps := i.Wait()
	timer.Stop()
	if !ps.Success() {
		t.Fatal("Failed to shutdown cleanly")
	}
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
