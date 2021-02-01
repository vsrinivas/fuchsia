// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package virtual_device

import (
	"errors"
	"fmt"
	"strings"

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

// QEMUCommand sets options to run Fuchsia in QEMU on the given QEMUCommandBuilder.
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
		switch {
		case fvd.Kernel != "" && image.Name == fvd.Kernel && image.Type == "kernel":
			kernel = image.Path
		case image.Name == fvd.Initrd && image.Type == "zbi":
			initrd = image.Path
		case fvd.Drive != nil && image.Name == fvd.Drive.Image && image.Type == "blk":
			drive = image.Path
		}
	}
	if fvd.Drive != nil {
		if fvd.Drive.IsFilename {
			// Drive is a path instead of an image name. Assume the caller verified it exists.
			drive = fvd.Drive.Image
		}
		b.AddVirtioBlkPciDrive(qemu.Drive{
			ID:   fvd.Drive.Id,
			Addr: fvd.Drive.PciAddress,
			File: drive,
		})
	}

	if kernel != "" {
		b.SetKernel(kernel)
	}

	b.SetInitrd(initrd)

	if fvd.Hw.Hci != "" {
		b.AddHCI(qemu.HCI(fvd.Hw.Hci))
	}

	for _, drive := range fvd.Hw.Drives {
		// TODO(kjharland): Refactor //tools/qemu to use a `Device` model and move this there.
		switch drive.Device.Model {
		case "usb-storage":
			b.AddUSBDrive(qemu.Drive{
				ID:   drive.Id,
				File: drive.Image,
				Addr: drive.PciAddress,
			})
		case "virtio-blk-pci":
			b.AddVirtioBlkPciDrive(qemu.Drive{
				ID:   drive.Id,
				File: drive.Image,
				Addr: drive.PciAddress,
			})
		default:
			return fmt.Errorf("unimplemented drive model: %q", drive.Device.Model)
		}
	}

	b.SetCPUCount(int(fvd.Hw.CpuCount))

	ramBytes, err := parseRAMBytes(fvd.Hw.Ram)
	if err != nil {
		return err
	}
	b.SetMemory(ramBytes)

	// The call to Validate() above guarantees the target is in this map.
	target := qemuTargets[fvd.Hw.Arch]
	b.SetTarget(target, fvd.Hw.EnableKvm)

	for _, d := range fvd.Hw.NetworkDevices {
		// TODO(kjharland): Switch all tests to -nic, which is newer than -netdev.
		netdev := qemu.Netdev{ID: d.Id}
		if d.Device != nil {
			netdev.Device = qemu.Device{Model: d.Device.Model}
			for _, option := range d.Device.Options {
				if opt := strings.SplitN(option, "=", 2); len(opt) == 2 {
					netdev.Device.AddOption(opt[0], opt[1])
				} else {
					return fmt.Errorf("invalid option: %q", option)
				}
			}
		}

		switch d.Kind {
		case "user":
			netdev.User = &qemu.NetdevUser{}
		case "tap":
			netdev.Tap = &qemu.NetdevTap{Name: d.Id}
		default:
			// TODO(kjharland): Check this in Validate()
			return fmt.Errorf("unimplemented network device kind: %q", d.Kind)
		}
		b.AddNetwork(netdev)
	}

	for _, arg := range fvd.KernelArgs {
		b.AddKernelArg(arg)
	}

	return nil
}
