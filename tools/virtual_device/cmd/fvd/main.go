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

	"github.com/golang/protobuf/jsonpb"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/virtual_device"
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

	fvd := virtual_device.Default()
	fvd.Hw.Arch = *targetCPU

	if err := virtual_device.Validate(fvd, images); err != nil {
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
