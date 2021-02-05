// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}

func ascenddPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "ascendd")
}

func startAscendd(t *testing.T) *exec.Cmd {
	n := rand.Uint64()
	path := fmt.Sprintf("/tmp/ascendd-for-serial-test.%v.sock", n)
	os.Setenv("ASCENDD", path)
	return exec.Command(ascenddPath(t), "--serial", "-", "--sockpath", path)
}

// Test that ascendd can connect to overnetstack via serial.
func TestOvernetSerial(t *testing.T) {
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
	device.Initrd = "overnet"
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog", "console.shell=false kernel.enable-debugging-syscalls=true", "kernel.enable-serial-syscalls=true")
	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	ascendd := startAscendd(t)
	if err = i.StartPiped(ascendd); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	if err = i.WaitForLogMessage("Established Client Overnet serial connection"); err != nil {
		t.Fatal(err)
	}
}

// Test that ascendd can connect to overnetstack via serial.
func TestNoSpinningIfNoSerial(t *testing.T) {
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
	device.Initrd = "overnet"
	device.KernelArgs = append(device.KernelArgs, "console.shell=false", "kernel.enable-debugging-syscalls=false", "kernel.enable-serial-syscalls=false")
	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	ascendd := startAscendd(t)
	if err = i.StartPiped(ascendd); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	if err = i.WaitForLogMessage("SERIAL LINK Debug completed with failure"); err != nil {
		t.Fatal(err)
	}
}
