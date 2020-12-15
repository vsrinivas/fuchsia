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

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
)

func TestShellEnabled(t *testing.T) {
	exPath := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	}()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           filepath.Join(exPath, "..", "fuchsia.zbi"),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=true",
	})

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	if err = i.WaitForLogMessage("console.shell: enabled"); err != nil {
		t.Fatal(err)
	}
	tokenFromSerial := randomTokenAsString(t)
	if err = i.RunCommand("echo '" + tokenFromSerial + "'"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage(tokenFromSerial); err != nil {
		t.Fatal(err)
	}
}

func TestAutorunEnabled(t *testing.T) {
	exPath := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exPath, "test_data"), emulator.DistributionParams{Emulator: emulator.Qemu})
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	}()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	tokenFromSerial := randomTokenAsString(t)
	i := distro.Create(emulator.Params{
		Arch: arch,
		ZBI:  filepath.Join(exPath, "..", "fuchsia.zbi"),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=true " +
			"zircon.autorun.boot=/boot/bin/sh+-c+echo+" + tokenFromSerial,
	})

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	}()

	// Wait for console-launcher to come up before waiting for the autorun output.
	if err = i.WaitForLogMessage("console-launcher: running"); err != nil {
		t.Fatal(err)
	}
	if err = i.WaitForLogMessage(tokenFromSerial); err != nil {
		t.Fatal(err)
	}
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
