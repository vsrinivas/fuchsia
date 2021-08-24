// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

var cmdline = []string{
	"kernel.halt-on-panic=true",
	"kernel.bypass-debuglog=true",
	"zircon.autorun.boot=/boot/bin/sh+-c+k",
}

const (
	cmdlineEntropy        = "kernel.entropy-mixin="
	cmdlineRequireEntropy = "kernel.cprng-seed-require.cmdline=true"
)

func removeCmdlineEntropy(args []string) []string {
	filteredArgs := make([]string, 0, len(args))
	for _, arg := range args {
		if strings.HasPrefix(arg, cmdlineEntropy) {
			continue
		}
		filteredArgs = append(filteredArgs, arg)
	}
	return filteredArgs
}

// See that the kernel doesn't boot if no cmdline entropy is provided.
func TestMissingCmdlineEntropyPanics(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireEntropy)

	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	i := distro.Create(device)
	i.Start()

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("Failed to seed PRNG from required entropy source: cmdline")
}

// See that the kernel doesn't boot if the cmdline entropy is incomplete.
func TestIncompleteCmdlineEntropyPanics(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireEntropy)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+"aabbcc")

	i := distro.Create(device)
	i.Start()

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("Failed to seed PRNG from required entropy source: cmdline")
}

// See that the kernel boots if enough cmdline entropy is provided.
func TestCmdlineEntropyBoots(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireEntropy)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	zeroEntropy := make([]byte, 64)

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+hex.EncodeToString(zeroEntropy))

	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
