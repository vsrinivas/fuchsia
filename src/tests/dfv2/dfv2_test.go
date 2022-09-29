// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/emulator"
	"go.fuchsia.dev/fuchsia/tools/emulator/emulatortest"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// Kernel commandline args to start in DFv2.
var cmdline = []string{
	"driver_manager.use_driver_framework_v2=true",
	"driver_manager.root-driver=fuchsia-boot:///#meta/platform-bus.cm",
}

func TestNetworking(t *testing.T) {
	exDir := execDir(t)
	distro := emulatortest.UnpackFrom(t, filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Femu,
	})
	arch := distro.TargetCPU()
	if arch != emulator.X64 {
		return
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	// Note: To run this test locally on linux, you must create the TAP interface:
	// $ sudo ip tuntap add mode tap qemu
	device.Hw.NetworkDevices = append(device.Hw.NetworkDevices, &fvdpb.Netdev{
		Id:     "qemu",
		Kind:   "tap",
		Device: &fvdpb.Device{Model: "virtio-net-pci"},
	})
	device.KernelArgs = append(device.KernelArgs, cmdline...)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	i := distro.CreateContext(ctx, device)
	i.Start()
	i.WaitForLogMessage("initializing platform")

	// Make sure the shell is ready to accept commands over serial.
	i.WaitForLogMessage("console.shell: enabled")

	// Ensure the network device comes up in DFv2.
	i.RunCommand("waitfor verbose class=network topo=/dev/; echo NETWORK_READY")
	i.WaitForLogMessage("NETWORK_READY")
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	return filepath.Dir(ex)
}
