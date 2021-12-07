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

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}

func startAscendd(t *testing.T, exDir string) *exec.Cmd {
	n := rand.Uint64()
	path := fmt.Sprintf("/tmp/ascendd-for-serial-test.%v.sock", n)
	os.Setenv("ASCENDD", path)
	return exec.Command(filepath.Join(exDir, "ascendd"), "--serial", "-", "--sockpath", path)
}

// Test that ascendd can connect to overnetstack via serial.
func TestOvernetSerial(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.Initrd = "overnet"
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog=true", "console.shell=false kernel.enable-debugging-syscalls=true", "kernel.enable-serial-syscalls=true")
	i := distro.Create(device)

	i.StartPiped(startAscendd(t, exDir))
	i.WaitForLogMessage("Established Client Overnet serial connection")
}

// Test that ascendd can connect to overnetstack via serial.
func TestNoSpinningIfNoSerial(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.Initrd = "overnet"
	device.KernelArgs = append(device.KernelArgs, "console.shell=false", "kernel.enable-debugging-syscalls=false", "kernel.enable-serial-syscalls=false")
	i := distro.Create(device)
	i.StartPiped(startAscendd(t, exDir))
	i.WaitForLogMessage("SERIAL LINK Debug completed with failure")
}
