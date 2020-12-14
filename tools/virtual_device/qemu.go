// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package virtual_device

import (
	"errors"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/qemu"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// qemuTargets maps a common Fuchsia CPU architecture string to its QEMU equivalent.
//
// This should provide a mapping for each value allowed by `isValidArch`.
var qemuTargets = map[string]qemu.Target{
	"x64":   qemu.TargetEnum.X86_64,
	"arm64": qemu.TargetEnum.AArch64,
}

// QEMUCommand sets options to run Fuchsia in QEMU using the given VirtualDevice.
//
// This returns an error if `Validate(fvd, images)` returns an error.
//
// This function is hermetic; It does not lookup or set the path to the QEMU binary, or
// interact with the filesystem or environment in anyway. These actions are left to the
// caller.
//
// The caller is free to set additional options on the builder before calling this function
// or after this function returns.
func QEMUCommand(b *qemu.QEMUCommandBuilder, fvd *fvdpb.VirtualDevice, images build.ImageManifest) error {
	if b == nil {
		return errors.New("QEMUCommandBuilder cannot be nil")
	}
	if len(images) == 0 {
		return errors.New("image manifest cannot be empty")
	}
	if err := Validate(fvd, images); err != nil {
		return err
	}

	// Image paths. The previous call to Validate() ensures these exist in the manifest.
	drive := ""
	initrd := ""
	kernel := ""
	for _, image := range images {
		switch image.Name {
		case fvd.Kernel:
			kernel = image.Path
		case fvd.Initrd:
			initrd = image.Path
		case fvd.Drive.Image:
			drive = image.Path
		}
	}
	if fvd.Drive.IsFilename {
		// Drive is a path instead of an image name. Assume the caller verified it exists.
		drive = fvd.Drive.Image
	}

	b.SetKernel(kernel)
	b.SetInitrd(initrd)
	b.AddVirtioBlkPciDrive(qemu.Drive{
		ID:   fvd.Drive.Id,
		Addr: fvd.Drive.PciAddress,
		File: drive,
	})

	b.SetCPUCount(int(fvd.Hw.CpuCount))

	ramBytes, err := parseRAMBytes(fvd.Hw.Ram)
	if err != nil {
		return err
	}
	b.SetMemory(ramBytes)

	// The call to Validate() above guarantees the target is in this map.
	target := qemuTargets[fvd.Hw.Arch]
	if err := b.SetTarget(target, fvd.Hw.EnableKvm); err != nil {
		return err
	}

	netdev := qemu.Netdev{ID: "netdev0", MAC: fvd.Hw.Mac}
	if fvd.Hw.Tap == nil || fvd.Hw.Tap.Name == "" {
		netdev.User = &qemu.NetdevUser{}
	} else {
		netdev.Tap = &qemu.NetdevTap{Name: fvd.Hw.Tap.Name}
	}
	b.AddNetwork(netdev)

	b.AddKernelArg("zircon.nodename=" + fvd.Nodename)

	return nil
}
