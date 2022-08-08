// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

var cmdlineCommon = []string{"kernel.oom.behavior=reboot", "kernel.oom.reboot-timeout-ms=0x000a"}

const hostTestDataDir = "test_data"

// Determines if the VM boots successfully.
func TestBoot(t *testing.T) {
	exDir := execDir(t)
	testDataPath := filepath.Join(exDir, hostTestDataDir)
	fvmBinary := filepath.Join(testDataPath, "tools", "fvm")
	vmConfigPath := filepath.Join(testDataPath, "config", "vm_config.json")

	distro := emulatortest.UnpackFrom(t, testDataPath, emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})

	vmConfig := struct {
		RamSize  string `json:"ram_size"`
		CpuCount uint32 `json:"cpu_count"`
	}{}
	if err := jsonutil.ReadFromFile(vmConfigPath, &vmConfig); err != nil {
		t.Fatalf("Cannont read VM config %q: %v", vmConfigPath, err)
	}
	resized := distro.ResizeRawImage("storage-full", fvmBinary)
	defer os.Remove(resized)
	arch := distro.TargetCPU()
	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, cmdlineCommon...)
	device.Hw.Ram = vmConfig.RamSize
	device.Hw.CpuCount = vmConfig.CpuCount
	device.Drive = &fvdpb.Drive{
		Id:         "maindisk",
		Image:      resized,
		IsFilename: true,
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()

	// Watch for happy signal, asserting failure if the device reboots due to OOM.
	i.WaitForLogMessageAssertNotSeen("Received startup request.", "ZIRCON REBOOT REASON (OOM)")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
