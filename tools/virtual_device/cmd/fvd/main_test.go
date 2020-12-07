// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

func TestValidateFVD(t *testing.T) {
	// An ImageManifest shared by all test cases. This is not modified during testing.
	testImageManifest := build.ImageManifest{
		{Name: "qemu-kernel", Path: "/kernel"},
		{Name: "storage-full", Path: "/fvm"},
		{Name: "zircon-a", Path: "/ramdisk"},
	}

	// Test cases.
	//
	// setupFVD is used to initialize the FVD to pass to validateFVD. It is called with a
	// known-good FVD as input, during test setup.
	tests := []struct {
		name     string
		setupFVD func(*fvdpb.VirtualDevice)
		wantErr  bool
	}{{
		name:     "ok",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { return },
	}, {
		name:     "missing kernel",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Kernel = "" },
		wantErr:  true,
	}, {
		name:     "missing drive",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Drive.Image = "" },
		wantErr:  true,
	}, {
		name:     "missing initial ramdisk",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Initrd = "" },
		wantErr:  true,
	}, {
		name:     "invalid ram",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Hw.Ram = "-1G" },
		wantErr:  true,
	}, {
		name:     "invalid arch",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Hw.Arch = "MIPS" },
		wantErr:  true,
	}, {
		name:     "invalid mac",
		setupFVD: func(fvd *fvdpb.VirtualDevice) { fvd.Hw.Mac = "0::12345" },
		wantErr:  true,
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			inputFVD := knownGoodFVD()
			tt.setupFVD(inputFVD)

			if err := validateFVD(inputFVD, testImageManifest); err != nil != tt.wantErr {
				if tt.wantErr {
					t.Fatalf("wanted an error but got nil")
				}
				t.Fatalf("got error %v", err)
			}
		})
	}
}

func TestIsValidRAM(t *testing.T) {
	tests := []struct {
		ram  string
		want bool
	}{
		{"0", false},
		{"1", false},
		{"1Z", false},
		{"b", false},
		{"-22G", false},

		{"0b", true},
		{"0B", true},
		{"1k", true},
		{"1K", true},
		{"16m", true},
		{"16M", true},
		{"256g", true},
		{"256G", true},
	}
	for _, tt := range tests {
		t.Run(tt.ram, func(t *testing.T) {
			if isValidRAM(tt.ram) != tt.want {
				t.Fatalf("isValidRAM(%q) got %v but wanted %v", tt.ram, !tt.want, tt.want)
			}
		})
	}
}

func TestIsValidArch(t *testing.T) {
	tests := []struct {
		arch string
		want bool
	}{
		{"MIPS", false},

		{"x64", true},
		{"arm64", true},
	}
	for _, tt := range tests {
		t.Run(tt.arch, func(t *testing.T) {
			if isValidArch(tt.arch) != tt.want {
				t.Fatalf("isValidArch(%q) got %v but wanted %v", tt.arch, !tt.want, tt.want)
			}
		})
	}
}

func TestIsValidMAC(t *testing.T) {
	tests := []struct {
		mac  string
		want bool
	}{
		{"00", false},
		{"00:11", false},
		{"00:11:22", false},
		{"00:11:22:33", false},
		{"00:11:22:33:44", false},
		{"00:11:22:33:44:55:", false},
		{":00:11:22:33:44:55:", false},
		{"00:11:22:33:44:55:66", false},
		{"00:11:22:33:44:55:66", false},

		{"00:11:22:33:44:55", true},
		{"7d:af:25:e3:dd:0b", true},
	}
	for _, tt := range tests {
		t.Run(tt.mac, func(t *testing.T) {
			if isValidMAC(tt.mac) != tt.want {
				t.Fatalf("isValidMAC(%q) got %v but wanted %v", tt.mac, !tt.want, tt.want)
			}
		})
	}
}

func knownGoodFVD() *fvdpb.VirtualDevice {
	return &fvdpb.VirtualDevice{
		Kernel: "qemu-kernel",
		Drive:  &fvdpb.Drive{Image: "storage-full"},
		Initrd: "zircon-a",
		Hw: &fvdpb.HardwareProfile{
			Ram: "5G",
		},
	}
}
