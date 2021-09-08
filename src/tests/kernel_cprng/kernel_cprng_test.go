// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/hex"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
)

var cmdline = []string{
	"kernel.halt-on-panic=true",
	"kernel.bypass-debuglog=true",
}

const (
	cmdlineEntropy                    = "kernel.entropy-mixin="
	cmdlineRequireEntropy             = "kernel.cprng-seed-require.cmdline=true"
	cmdlineRequireJitterEntropy       = "kernel.cprng-seed-require.jitterentropy=true"
	cmdlineRequireJitterEntropyReseed = "kernel.cprng-reseed-require.jitterentropy=true"
	cmdlineDisableHWRNG               = "kernel.cprng-disable.hw-rng=true"
	cmdlineDisableJitterEntropy       = "kernel.cprng-disable.jitterentropy=true"
	cmdlineAutorunShK                 = "zircon.autorun.boot=/boot/bin/sh+-c+k"

	CPRNG_DRAWS = 32
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
	device.KernelArgs = append(device.KernelArgs, cmdlineAutorunShK)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	zeroEntropy := make([]byte, 64)

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+hex.EncodeToString(zeroEntropy))

	i := distro.Create(device)
	i.Start()

	// Wait for the system to finish booting.
	i.WaitForLogMessage("usage: k <command>")
}

func captureCPRNGDraws(t *testing.T, entropy []byte, extraKernelArgs []string) map[string]bool {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)
	device.KernelArgs = append(device.KernelArgs, extraKernelArgs...)
	device.Initrd = "cprng-draw-zbi"

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+hex.EncodeToString(entropy))

	i := distro.Create(device)
	i.Start()
	lines := i.CaptureLinesContaining("cprng-draw{", "-- cprng-draw-end --")
	if len(lines) != CPRNG_DRAWS {
		t.Fatalf("could not capture all cprng draws. want %d, got %d", CPRNG_DRAWS, len(lines))
	}

	draws := make(map[string]bool)
	CPRNGDrawRe := regexp.MustCompile(`cprng-draw\{[a-f0-9]{64}\}`)
	for _, line := range lines {
		draw := string(CPRNGDrawRe.Find([]byte(line)))
		if draw == "" {
			t.Fatalf("could not parse line: %q", line)
		}
		if _, found := draws[draw]; found {
			t.Fatalf("repeated draws in cprng. line: %q", draw)
		}
		draws[draw] = true
	}

	return draws
}

func TestDifferentEntropyDifferentDraws(t *testing.T) {
	extraKernelArgs := []string{
		cmdlineRequireEntropy,
		cmdlineDisableHWRNG,
		cmdlineDisableJitterEntropy,
	}

	// Run with all-zero entropy.
	entropy := make([]byte, 64)
	draws1 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	// Run again, but with different entropy.
	entropy[0] = 0x1
	draws2 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	// None of the draws can appear in both outputs.
	for draw := range draws1 {
		if _, found := draws2[draw]; found {
			t.Fatalf("found repeated draw: %q", draw)
		}
	}
}

func TestSameEntropySameDraws(t *testing.T) {
	extraKernelArgs := []string{
		cmdlineRequireEntropy,
		cmdlineDisableHWRNG,
		cmdlineDisableJitterEntropy,
	}

	// Run with all-zero entropy.
	entropy := make([]byte, 64)
	draws1 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	// Run again with all-zero entropy.
	entropy = make([]byte, 64)
	draws2 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	// All of the draws must be the same.
	// This assumes the nobody uses the cprng in between.
	for draw := range draws1 {
		if _, found := draws2[draw]; !found {
			t.Fatalf("found missing draw: %q", draw)
		}
	}
}

func TestJitterEntropyRequiredGivesDifferentEntropyThanOnlyCmdline(t *testing.T) {
	// JitterEntropy collection on x86-64 requires Invariant TSC Feature
	// which is only supported on KVM enabled hosts.
	extraKernelArgs := []string{
		cmdlineDisableHWRNG,
		cmdlineRequireEntropy,
		cmdlineDisableJitterEntropy,
	}
	// Run with all-zero entropy.
	entropy := make([]byte, 64)
	draws1 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	extraKernelArgs = []string{
		cmdlineDisableHWRNG,
		cmdlineRequireEntropy,
		cmdlineRequireJitterEntropy,
	}
	// Run with all-zero entropy.
	entropy = make([]byte, 64)
	draws2 := captureCPRNGDraws(t, entropy, extraKernelArgs)

	// None of the draws can appear in both outputs.
	for draw := range draws1 {
		if _, found := draws2[draw]; found {
			t.Fatalf("found repeated draw: %q", draw)
		}
	}
}

func TestDisabledJitterEntropyAndRequiredForReseedDoesntReachUserspace(t *testing.T) {
	// This test checks that a system that requires JitterEntropy on Reseed doesn't
	// execute userspace programs.
	// This is used to test that a reseed occurs before executing the first userspace program.
	// Note that drawing from JitterEntropy requires Invariant TSC which
	// is only available in KVM.

	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))

	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireEntropy)
	device.KernelArgs = append(device.KernelArgs, cmdlineAutorunShK)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	zeroEntropy := make([]byte, 64)

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+hex.EncodeToString(zeroEntropy))

	// Disable JitterEntropy but also require for Reseed.
	// Given that there is a Reseed before entering userspace, the system should never reach userspace.
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireJitterEntropyReseed)
	device.KernelArgs = append(device.KernelArgs, cmdlineDisableJitterEntropy)

	i := distro.Create(device)

	// This test only makes sense if we are using KVM, as JitterEntropy is only
	// available if we have Invalid TSC.
	i.Start()

	lines := i.CaptureLinesContaining("usage: k <command>", "ZIRCON KERNEL PANIC")
	if len(lines) != 0 {
		t.Fatalf("device reached userspace")
	}
	i.WaitForLogMessage("Failed to reseed PRNG from required entropy source: jitterentropy")
}

func TestDisabledJitterEntropyAndRequiredDoesntBoot(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdline...)
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireEntropy)
	device.KernelArgs = append(device.KernelArgs, cmdlineAutorunShK)
	device.KernelArgs = removeCmdlineEntropy(device.KernelArgs)

	zeroEntropy := make([]byte, 64)

	device.KernelArgs = append(device.KernelArgs, cmdlineEntropy+hex.EncodeToString(zeroEntropy))

	// Disable JitterEntropy but also require it. It should not boot.
	device.KernelArgs = append(device.KernelArgs, cmdlineRequireJitterEntropy)
	device.KernelArgs = append(device.KernelArgs, cmdlineDisableJitterEntropy)

	i := distro.Create(device)

	// This test only makes sense if we are using KVM, as JitterEntropy is only
	// available if we have Invalid TSC.
	i.Start()

	// See that it panicked.
	i.WaitForLogMessage("ZIRCON KERNEL PANIC")
	i.WaitForLogMessage("Failed to seed PRNG from required entropy source: jitterentropy")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
