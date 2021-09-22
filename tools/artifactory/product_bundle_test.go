// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
	"io/ioutil"
	"path/filepath"
	"testing"
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

	expectedProductBundle, err := json.MarshalIndent(ProductBundle{
		SchemaID: "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
		Data: Data{
			Description: "some emulator device",
			DeviceRefs:  []string{"qemu-x64"},
			Images: []*Image{
				{
					BaseURI: "file:/..",
					Format:  "files",
				},
			},
			Type: "product_bundle",
			Name: "core.qemu-x64",
			Packages: []*Package{
				{
					Format:  "files",
					RepoURI: "file:/../../packages",
					BlobURI: "file:/../../blobs",
				},
			},
			Manifests: &Manifests{
				Emu: &EmuManifest{
					DiskImages:     []string{"obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"},
					InitialRamdisk: "fuchsia.zbi",
					Kernel:         "multiboot.bin",
				},
			},
		},
	}, "", "  ")
	if err != nil {
		t.Fatalf("unable to generate expected product bundle: %v", err)
	}

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
				Contents:    expectedProductBundle,
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
