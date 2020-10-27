// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"archive/tar"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements imgModules
type mockModules struct {
	imgs     []build.Image
	buildDir string
}

func (m mockModules) BuildDir() string {
	return m.buildDir
}

func (m mockModules) ImageManifest() string {
	return "BUILD_DIR/IMAGE_MANIFEST"
}

func (m mockModules) Images() []build.Image {
	return m.imgs
}

func TestImageUploads(t *testing.T) {
	// Create a temporary disk.raw image.
	dir, err := ioutil.TempDir("", "testBuildDir")
	if err != nil {
		t.Fatalf("failed to create fake build dir: %s", err)
	}
	defer os.RemoveAll(dir)
	f, err := ioutil.TempFile(dir, "disk.raw")
	if err != nil {
		t.Fatalf("failed to create fake disk.raw: %s", err)
	}
	defer f.Close()
	if _, err := io.WriteString(f, "Hello World!"); err != nil {
		t.Fatalf("failed to write to fake disk.raw file: %s", err)
	}
	if err := f.Sync(); err != nil {
		t.Fatalf("failed to sync fake disk.raw: %s", err)
	}
	info, err := f.Stat()
	if err != nil {
		t.Fatalf("failed to get file info for fake disk.raw: %s", err)
	}
	m := &mockModules{
		buildDir: dir,
		imgs: []build.Image{
			{
				PaveArgs: []string{"--bootloader"},
				Name:     "bootloader",
				Path:     "bootloader",
				Type:     "blk",
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
				Name: "uefi-disk",
				Path: filepath.Base(f.Name()),
				Type: "blk",
			},
		},
	}
	expected := []Upload{
		{
			Source:      "BUILD_DIR/IMAGE_MANIFEST",
			Destination: "namespace/IMAGE_MANIFEST",
		},
		{
			Source:      filepath.Join(dir, "bootloader"),
			Destination: "namespace/bootloader",
			Compress:    true,
		},
		{
			Source:      filepath.Join(dir, "zedboot.zbi"),
			Destination: "namespace/zedboot.zbi",
			Compress:    true,
		},
		{
			Source:      filepath.Join(dir, "fuchsia.zbi"),
			Destination: "namespace/fuchsia.zbi",
			Compress:    true,
		},
		{
			Source:      filepath.Join(dir, "qemu-kernel.bin"),
			Destination: "namespace/qemu-kernel.bin",
			Compress:    true,
		},
		{
			Source:      f.Name(),
			Destination: filepath.Join("namespace", gceUploadName),
			Compress:    true,
			TarHeader: &tar.Header{
				Format: tar.FormatGNU,
				Name:   gceImageName,
				Mode:   0666,
				Size:   info.Size(),
			},
		},
	}
	actual, err := imageUploads(m, "namespace")
	if err != nil {
		t.Fatalf("imageUploads failed: %s", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected image uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
