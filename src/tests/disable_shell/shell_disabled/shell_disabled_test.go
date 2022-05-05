// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

func TestShellDisabled(t *testing.T) {
	exPath := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, "console.shell=false")
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()
	i.WaitForLogMessage("console.shell: disabled")
	tokenFromSerial := randomTokenAsString(t)
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.AssertLogMessageNotSeenWithinTimeout(tokenFromSerial, 3*time.Second)
}

func TestAutorunDisabled(t *testing.T) {
	exPath := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, "console.shell=false", "zircon.autorun.boot=foobar")
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()
	i.WaitForLogMessage("cannot launch autorun command 'foobar'")
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
