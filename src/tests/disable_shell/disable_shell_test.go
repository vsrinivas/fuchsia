// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/rand"
	"fmt"
	"fuchsia.googlesource.com/testing/qemu"
	"os"
	"path/filepath"
	"testing"
	"time"
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

func randomTokenAsString() string {
	b := make([]byte, 32)
	_, err := rand.Read(b)
	if err != nil {
		panic(err)
	}

	ret := ""
	for i := 0; i < 32; i++ {
		ret += fmt.Sprintf("%x", b[i])
	}
	return ret
}

func TestShellDisabled(t *testing.T) {
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
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "console.shell=false",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	time.Sleep(time.Second)
	tokenFromSerial := randomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessageAssertNotSeen("console.shell: disabled", tokenFromSerial)
}

func TestShellEnabled(t *testing.T) {
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
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: "console.shell=true",
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}
	defer i.Kill()

	time.Sleep(time.Second)
	tokenFromSerial := randomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessage(tokenFromSerial)
}

// The default is enabled in bringup.gni.
func TestShellDefault(t *testing.T) {
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

	time.Sleep(time.Second)
	tokenFromSerial := randomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")
	i.WaitForLogMessage(tokenFromSerial)
}
