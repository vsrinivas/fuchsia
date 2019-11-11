// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestImageUploads(t *testing.T) {
	var mockManifest = `[
  {
    "bootserver_pave": [
      "--bootloader"
    ],
    "name": "bootloader",
    "path": "bootloader"
  },
  {
    "bootserver_pave": [
      "--zirconr"
    ],
    "name": "zircon-r",
    "path": "zedboot.zbi",
    "type": "zbi"
  },
  {
    "bootserver_pave": [
      "--zircona"
    ],
    "name": "zircon-a",
    "path": "fuchsia.zbi",
    "type": "zbi"
  },
  {
    "bootserver_netboot": [
      "--boot"
    ],
    "name": "fuchsia",
    "path": "fuchsia.zbi",
    "type": "zbi"
  },
  {
    "name": "qemu-kernel",
    "path": "qemu-kernel.bin",
    "type": "kernel"
  },
  {
    "name": "pave",
    "path": "pave.sh",
    "type": "sh"
  }
]`
	tmpDir, err := ioutil.TempDir("", "test-data")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	imgManifest := filepath.Join(tmpDir, "images.json")
	if err := ioutil.WriteFile(imgManifest, []byte(mockManifest), 0444); err != nil {
		t.Fatalf("failed to write image manifest: %v", err)
	}
	expected := []Upload{
		{
			Source:      imgManifest,
			Destination: "namespace/images.json",
		},
		{
			Source:      filepath.Join(tmpDir, "bootloader"),
			Destination: "namespace/bootloader",
		},
		{
			Source:      filepath.Join(tmpDir, "zedboot.zbi"),
			Destination: "namespace/zedboot.zbi",
		},
		{
			Source:      filepath.Join(tmpDir, "fuchsia.zbi"),
			Destination: "namespace/fuchsia.zbi",
		},
		{
			Source:      filepath.Join(tmpDir, "qemu-kernel.bin"),
			Destination: "namespace/qemu-kernel.bin",
		},
	}
	actual, err := ImageUploads(tmpDir, "namespace")
	if err != nil {
		t.Fatalf("failed to get image Uploads: %v", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected image uploads: actual: %v, expected: %v", actual, expected)
	}
}
