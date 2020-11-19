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

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../obj/build/images/overnet/overnet.zbi")
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
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "devmgr.log-to-debuglog console.shell=false kernel.enable-debugging-syscalls=true kernel.enable-serial-syscalls=true",
	})

	ascendd := startAscendd(t)
	err = i.StartPiped(ascendd)
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("Established Client Overnet serial connection")
}

// Test that ascendd can connect to overnetstack via serial.
func TestNoSpinningIfNoSerial(t *testing.T) {
	distro, err := emulator.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(emulator.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "console.shell=false kernel.enable-debugging-syscalls=false kernel.enable-serial-syscalls=false",
	})

	ascendd := startAscendd(t)
	err = i.StartPiped(ascendd)
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	i.WaitForLogMessage("SERIAL LINK Debug completed with failure")
}
