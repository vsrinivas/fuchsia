// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// fvd generates a Fuchsia Virtual Device (.fvd) file for the current
// product x arch image being built. It is meant to be run during the build.
package main

// TODO(kjharland): Set QEMU metadata.

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"

	"github.com/golang/protobuf/jsonpb"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// The names of images to write to the FVD.
//
// In the future these might be configurable but for now we use the same images
// for all emulator product x arch combinations.
const (
	imageNameKernel         = "qemu-kernel"
	imageNameFVM            = "storage-full"
	imageNameInitialRamdisk = "zircon-a"
)

func main() {
	if err := mainImpl(); err != nil {
		fmt.Fprintf(os.Stderr, "fvd: %v\n", err)
		os.Exit(1)
	}
}

func mainImpl() error {
	imagesJSONPath := flag.String("images_json", "", "The path to images.json")
	outputPath := flag.String("output", "", "Where to write the .fvd file (Default: stdout)")
	fvdName := flag.String("name", "default", "The virtual device name")
	flag.Parse()

	if *imagesJSONPath == "" {
		return errors.New("missing --images_json")
	}
	if *fvdName == "" {
		return errors.New("missing --name")
	}

	images, err := readImagesJSON(*imagesJSONPath)
	if err != nil {
		return err
	}

	fvd, err := generate(*fvdName, images)

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

func generate(name string, images imagesJSON) (*fvdpb.VirtualDevice, error) {
	fvd := &fvdpb.VirtualDevice{Name: name}

	// Ensure the image names written into the FVD are present in images.json.
	for _, entry := range images {
		switch entry.Name {
		case imageNameKernel:
			fvd.Kernel = entry.Name
		case imageNameFVM:
			fvd.Fvm = entry.Name
		case imageNameInitialRamdisk:
			fvd.Initrd = entry.Name
		}
	}

	if fvd.Kernel == "" {
		return nil, errors.New("kernel image not found")
	}
	if fvd.Fvm == "" {
		return nil, errors.New("fvm image not found")
	}
	if fvd.Initrd == "" {
		return nil, errors.New("initial ramdisk not found")
	}

	return fvd, nil
}

// scope represents a JSON object entry in images.json.
//
// See `build_api_module("images")` in /BUILD.gn for documentation on the full
// set of fields that may appear in the scope. This is only a subset needed to
// generate the FVD proto.
type scope struct {
	Name string `json:"name"`
	Path string `json:"path"`
}

type imagesJSON []scope

func readImagesJSON(filename string) (imagesJSON, error) {
	bytes, err := ioutil.ReadFile(filename)
	if err != nil {
		return nil, err
	}
	var i imagesJSON
	if err := json.Unmarshal(bytes, &i); err != nil {
		return nil, err
	}
	return i, nil
}
