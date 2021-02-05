// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

func TestShutdown(t *testing.T) {
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
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog")

	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		// Ignore the error value, since it'll return an error if the other Kill()
		// call below ran, which normally happens.
		_ = i.Kill()
	}()

	if err = i.WaitForLogMessage("initializing platform"); err != nil {
		t.Fatal(err)
	}

	// Make sure the shell is ready to accept commands over serial.
	if err = i.WaitForLogMessage("console.shell: enabled"); err != nil {
		t.Fatal(err)
	}

	if arch == emulator.X64 {
		// Ensure the ACPI driver comes up before we attempt a shutdown.
		if err = i.RunCommand("waitfor class=acpi topo=/dev/sys/platform/acpi; echo ACPI_READY"); err != nil {
			t.Fatal(err)
		}
		if err = i.WaitForLogMessage("ACPI_READY"); err != nil {
			t.Fatal(err)
		}
	}

	// Trigger a shutdown.
	if err = i.RunCommand("dm shutdown"); err != nil {
		t.Fatal(err)
	}

	// Start a timer so we can abort the wait by explicitly killing. This will yield a nice error
	// from the wait command that we can detect.
	timer := time.AfterFunc(120*time.Second, func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	})

	// Cannot check for log messages as we are racing with the shutdown, and if qemu closes first
	// checking for log messages will panic, so we just wait.
	ps, err := i.Wait()
	timer.Stop()
	if err != nil {
		t.Fatal(err)
	}
	if !ps.Success() {
		t.Fatal("Failed to shutdown cleanly")
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
