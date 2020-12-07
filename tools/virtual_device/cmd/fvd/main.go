// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// fvd generates a Fuchsia Virtual Device (.fvd) file during the build.
//
// The FVD is generated from a default struct representing the final output,
// build metadata, and the build's image.json manifest.
//
// This binary also validates the properties of the generated FVD and sets
// default values where appropriate, even though launchers will often do so at
// runtime.
//
// For the FVD schema, see //tools/virtual_device/proto/virtual_device.proto.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"regexp"

	"github.com/golang/protobuf/jsonpb"
	"go.fuchsia.dev/fuchsia/tools/build"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// Regular expressions for validating FVD properties.
var (
	// ramRe matches a system RAM size description, like '50G'.
	//
	// See //tools/virtual_device/proto/virtual_device.proto for a full description of the
	// format.
	ramRe = regexp.MustCompile(`^[0-9]+[bBkKmMgG]$`)

	// macRe matches a MAC address.
	macRe = regexp.MustCompile(`^([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}$`)
)

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "fvd: %v\n", err)
		os.Exit(1)
	}
}

func mainImpl() error {
	imageManifestPath := flag.String("images_json", "", "The path to images.json")
	outputPath := flag.String("output", "", "Where to write the .fvd file (Default: stdout)")
	targetCPU := flag.String("buildinfo_cpu", "", "The target cpu architecture")

	flag.Parse()

	if *imageManifestPath == "" {
		return errors.New("missing -images_json")
	}
	if *targetCPU == "" {
		return errors.New("missing -buildinfo_cpu")
	}

	images, err := build.LoadImages(*imageManifestPath)
	if err != nil {
		return err
	}

	fvd := &fvdpb.VirtualDevice{
		Name:     "default",
		Nodename: "fuchsia-virtual-device",
		Kernel:   "qemu-kernel",
		Initrd:   "zircon-a",
		Drive: &fvdpb.Drive{
			Id:    "maindisk",
			Image: "storage-full",
		},
		Hw: &fvdpb.HardwareProfile{
			Arch:     *targetCPU,
			CpuCount: 1,
			Ram:      "1M",
			Mac:      "52:54:00:63:5e:7a",
		},
	}
	if err := validateFVD(fvd, images); err != nil {
		return fmt.Errorf("invalid FVD: %w", err)
	}

	var out io.Writer = os.Stdout
	if *outputPath != "" {
		fd, err := os.Create(*outputPath)
		if err != nil {
			return err
		}
		defer fd.Close()
		out = fd
	}

	m := jsonpb.Marshaler{Indent: "  "}
	return m.Marshal(out, fvd)
}

// TODO(kjharland): Move this to a common pkg so launchers can call it at runtime.
func validateFVD(fvd *fvdpb.VirtualDevice, images build.ImageManifest) error {
	// Ensure the images referenced in the FVD exist in the image manifest.
	var exists = struct{}{}
	found := map[string]struct{}{}

	for _, image := range images {
		found[image.Name] = exists
	}
	if _, ok := found[fvd.Kernel]; !ok {
		return fmt.Errorf("kernel image %q not found", fvd.Kernel)
	}
	if _, ok := found[fvd.Initrd]; !ok {
		return fmt.Errorf("initial ramdisk image %q not found", fvd.Initrd)
	}

	// If drive points to a file instead of an entry in the image manifest, the
	// filepath will be checked at runtime instead, since it may not exist at
	// the moment, or it may be a relative path that only makes sense at runtime
	// (e.g. a MinFS image that is created during a test run).
	if !fvd.Drive.IsFilename {
		if _, ok := found[fvd.Drive.Image]; !ok {
			return fmt.Errorf("drive image %q not found", fvd.Drive.Image)
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
	return ram == "" || ramRe.MatchString(ram)
}

func isValidArch(arch string) bool {
	return arch == "" || arch == "x64" || arch == "arm64"
}

func isValidMAC(mac string) bool {
	return mac == "" || macRe.MatchString(mac)
}
