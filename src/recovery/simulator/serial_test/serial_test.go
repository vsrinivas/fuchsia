// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package simulator

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

// TestSerial verifies that the serial shell is enabled for recovery-eng.
func TestSerialShellEnabled(t *testing.T) {
	exPath := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.Initrd = "recovery-eng"
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()
	i.WaitForLogMessage("launching /boot/bin/sh (sh:console)")
	tokenFromSerial := randomTokenAsString(t)
	i.RunCommand("echo '" + tokenFromSerial + "'")
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
