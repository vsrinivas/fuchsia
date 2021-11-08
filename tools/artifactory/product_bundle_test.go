// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"io/ioutil"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"

	"github.com/google/go-cmp/cmp"
)

func TestProductBundleUploads(t *testing.T) {
	productBundleData := []byte(`{
  "data": {
    "description": "some emulator device",
    "device_refs": [
      "qemu-x64"
    ],
    "images": [
      {
        "base_uri": "file:/../../..",
        "format": "files"
      }
    ],
    "name": "core.qemu-x64",
    "packages": [
      {
        "format": "files",
        "repo_uri": "file:/../../../../amber-files"
      }
    ],
    "type": "product_bundle",
    "manifests": {
      "emu": {
        "disk_images": [
          "obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"
        ],
        "initial_ramdisk": "fuchsia.zbi",
        "kernel": "multiboot.bin"
	  }
	}
  },
  "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
}`)
	dir := t.TempDir()
	productBundlePath := filepath.Join(dir, "product_bundle.json")
	if err := ioutil.WriteFile(productBundlePath, productBundleData, 0o600); err != nil {
		t.Fatalf("failed to write to fake product_bundle.json file: %v", err)
	}

	expectedProductBundle := []byte(`{
  "data": {
    "device_refs": [
      "qemu-x64"
    ],
    "images": [
      {
        "base_uri": "file:/..",
        "format": "files"
      }
    ],
    "type": "product_bundle",
    "name": "core.qemu-x64",
    "packages": [
      {
        "format": "files",
        "blob_uri": "file:/../../blobs",
        "repo_uri": "file:/../../packages"
      }
    ],
    "description": "some emulator device",
    "manifests": {
      "emu": {
        "disk_images": [
          "obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"
        ],
        "initial_ramdisk": "fuchsia.zbi",
        "kernel": "multiboot.bin"
      }
    }
  },
  "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
}`)

	var tests = []struct {
		name string
		m    *mockModules
		want *Upload
	}{
		{
			name: "valid product bundle",
			m: &mockModules{
				buildDir: dir,
				imgs: []build.Image{
					{
						Name: "product_bundle",
						Path: "product_bundle.json",
						Type: "manifest",
					},
				},
			},
			want: &Upload{
				Compress:    true,
				Destination: filepath.Join(dir, "images", "product_bundle.json"),
				Contents:    []byte(expectedProductBundle),
			},
		},
		{
			name: "product bundle doesn't exist in manifest",
			m: &mockModules{
				buildDir: dir,
				imgs: []build.Image{
					{
						Name: "uefi-disk",
						Path: "disk.raw",
						Type: "blk",
					},
				},
			},
		},
	}

	packageNamespaceDir := filepath.Join(dir, "packages/")
	blobNamespaceDir := filepath.Join(dir, "blobs/")
	imageNamespaceDir := filepath.Join(dir, "images/")

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			got, err := productBundleUploads(test.m, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir)
			if err != nil {
				t.Errorf("productBundleUploads failed: %v", err)
			}
			if diff := cmp.Diff(test.want, got); diff != "" {
				t.Errorf("unexpected image uploads (-want +got):\n%v", diff)
			}
		})
	}
}
