// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package virtual_device

import (
	"errors"
	"fmt"
	"math"
	"regexp"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// Regular expressions for validating FVD properties.
var (
	// ramRe matches a system RAM size description, like '50G'.
	//
	// FindStringSubmatch captures three groups from a valid string: The first is the
	// entire string, the second and third are the ram number and units respectively.
	//
	// See //tools/virtual_device/proto/virtual_device.proto for a full description of the
	// format.
	ramRe = regexp.MustCompile(`^([0-9]+)([bBkKmMgG])$`)

	// macRe matches a MAC address.
	macRe = regexp.MustCompile(`^([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$`)
)

// Default returns a VirtualDevice with default values.
func Default() *fvdpb.VirtualDevice {
	return &fvdpb.VirtualDevice{
		Name:     "default",
		Nodename: "fuchsia-virtual-device",
		Kernel:   "qemu-kernel",
		Initrd:   "zircon-a",
		Drive: &fvdpb.Drive{
			Id:    "maindisk",
			Image: "storage-full",
		},
		Hw: &fvdpb.HardwareProfile{
			Arch:     "x64",
			CpuCount: 1,
			Ram:      "1M",
			Mac:      "52:54:00:63:5e:7a",
		},
	}
}

// Validate returns nil iff the given FVD is valid for the given ImageManifest.
//
// All system images referenced in the FVD must exist in the image manifest.
func Validate(fvd *fvdpb.VirtualDevice, images build.ImageManifest) error {
	if fvd == nil {
		return errors.New("virtual device cannot be nil")
	}
	if fvd.Drive == nil {
		return errors.New("drive must not be nil")
	}
	if fvd.Nodename == "" {
		return errors.New("a nodename is required")
	}

	// Ensure the images referenced in the FVD exist in the image manifest.
	paths := map[string][]string{}
	for _, image := range images {
		paths[image.Name] = append(paths[image.Name], image.Path)
	}

	// A helper function that ensures an image exists in the manifest, has a unique
	// name within the manifest, and has a non-empty path.
	uniqueImageExists := func(name string) error {
		if imagePaths, ok := paths[name]; !ok {
			return fmt.Errorf("image %q not found", name)
		} else if len(imagePaths) != 1 {
			return fmt.Errorf("manifest contains multiple images named %q: %v", name, imagePaths)
		} else if imagePaths[0] == "" {
			return fmt.Errorf("no path specified for image %q", name)
		}
		return nil
	}

	if err := uniqueImageExists(fvd.Kernel); err != nil {
		return err
	}
	if err := uniqueImageExists(fvd.Initrd); err != nil {
		return err
	}

	// If drive points to a file instead of an entry in the image manifest, the filepath
	// will be checked at runtime instead since it may not exist when this function is
	// called (e.g. it could be a MinFS image which is created during a test run).
	if !fvd.Drive.IsFilename {
		if err := uniqueImageExists(fvd.Drive.Image); err != nil {
			return err
		}
	}

	if !isValidRAM(fvd.Hw.Ram) {
		return fmt.Errorf("invalid ram: %q", fvd.Hw.Ram)
	}
	if !isValidArch(fvd.Hw.Arch) {
		return fmt.Errorf("invalid arch: %q", fvd.Hw.Arch)
	}
	if !isValidMAC(fvd.Hw.Mac) {
		return fmt.Errorf("invalid MAC address: %q", fvd.Hw.Mac)
	}
	return nil
}

func isValidRAM(ram string) bool {
	return ramRe.MatchString(ram)
}

func isValidArch(arch string) bool {
	return arch == "x64" || arch == "arm64"
}

func isValidMAC(mac string) bool {
	return macRe.MatchString(mac)
}

func parseRAMBytes(ram string) (int, error) {
	if !isValidRAM(ram) {
		return -1, fmt.Errorf("invalid ram: %q", ram)
	}

	matches := ramRe.FindStringSubmatch(ram)
	size, err := strconv.ParseInt(matches[1], 10, 64)
	if err != nil {
		return -1, err
	}
	unit := strings.ToLower(matches[2])
	power := map[string]float64{"b": 0, "k": 1, "m": 2, "g": 3}[unit]
	bytes := int(size * int64(math.Pow(1024, power)))
	return bytes, nil
}
