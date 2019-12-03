// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Implements imgModules
type mockModules struct {
	imgs []build.Image
}

func (m mockModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockModules) ImageManifest() string {
	return "BUILD_DIR/IMAGE_MANIFEST"
}

func (m mockModules) Images() []build.Image {
	return m.imgs
}

func TestImageUploads(t *testing.T) {
	m := &mockModules{
		imgs: []build.Image{
			{
				PaveArgs: []string{"--bootloader"},
				Name:     "bootloader",
				Path:     "bootloader",
			},
			{
				PaveArgs: []string{"--zirconr"},
				Name:     "zircon-r",
				Path:     "zedboot.zbi",
				Type:     "zbi",
			},
			{
				PaveArgs: []string{"--zircona"},
				Name:     "zircon-a",
				Path:     "fuchsia.zbi",
				Type:     "zbi",
			},
			{
				NetbootArgs: []string{"--boot"},
				Name:        "fuchsia",
				Path:        "fuchsia.zbi",
				Type:        "zbi",
			},
			{
				Name: "qemu-kernel",
				Path: "qemu-kernel.bin",
				Type: "kernel",
			},
			{
				Name: "pave",
				Path: "pave.sh",
				Type: "sh",
			},
		},
	}

	expected := []Upload{
		{
			Source:      "BUILD_DIR/IMAGE_MANIFEST",
			Destination: "namespace/IMAGE_MANIFEST",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "bootloader"),
			Destination: "namespace/bootloader",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "zedboot.zbi"),
			Destination: "namespace/zedboot.zbi",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "fuchsia.zbi"),
			Destination: "namespace/fuchsia.zbi",
		},
		{
			Source:      filepath.Join("BUILD_DIR", "qemu-kernel.bin"),
			Destination: "namespace/qemu-kernel.bin",
		},
	}
	actual := imageUploads(m, "namespace")
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected image uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
