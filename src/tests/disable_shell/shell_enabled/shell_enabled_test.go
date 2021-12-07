// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/rand"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

func TestShellEnabled(t *testing.T) {
	exPath := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, "devmgr.log-to-debuglog=true", "console.shell=true")
	i := distro.Create(device)
	i.Start()
	i.WaitForLogMessage("console.shell: enabled")
	tokenFromSerial := randomTokenAsString(t)
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessage(tokenFromSerial)
}

func TestAutorunEnabled(t *testing.T) {
	exPath := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	arch := distro.TargetCPU()
	tokenFromSerial := randomTokenAsString(t)
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs,
		"devmgr.log-to-debuglog=true",
		"console.shell=true",
		"zircon.autorun.boot=/boot/bin/sh+-c+echo+"+tokenFromSerial)
	i := distro.Create(device)
	i.Start()

	// Wait for console-launcher to come up before waiting for the autorun output.
	i.WaitForLogMessage("console-launcher: running")
	i.WaitForLogMessage(tokenFromSerial)
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}

func randomTokenAsString(t *testing.T) string {
	b := [32]byte{}
	if _, err := rand.Read(b[:]); err != nil {
		t.Fatal(err)
	}
	return hex.EncodeToString(b[:])
}
