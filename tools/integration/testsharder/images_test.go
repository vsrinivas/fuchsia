// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
)

func mockImages(t *testing.T) ([]build.Image, string) {
	t.Helper()
	imgs := []build.Image{
		{
			PaveArgs: []string{"--boot", "--zircona"},
			Name:     "zircon-a",
			Path:     "fuchsia.zbi",
			Type:     "zbi",
		},
		{
			PaveZedbootArgs: []string{"--zirconr"},
			Name:            "zircon-r",
			Path:            "zedboot.zbi",
			Type:            "zbi",
		},
		{
			NetbootArgs: []string{"--boot"},
			Name:        "netboot",
			Path:        "netboot.zbi",
			Type:        "zbi",
		},
		{
			Name: "qemu-kernel",
			Path: "multiboot.bin",
			Type: "kernel",
		},
		{
			Name: "storage-full",
			Path: "obj/build/images/fuchsia/fuchsia/fvm.blk",
			Type: "blk",
		},
		{
			Name: "zbi-image",
			Path: "zbi-image.zbi",
			Type: "zbi",
		},
		{
			Name: "vbmeta-image",
			Path: "vbmeta-image.vbmeta",
			Type: "vbmeta",
		},
		{
			Name:  "other-qemu-kernel",
			Label: "//:other-qemu-kernel",
			Path:  "other-qemu-kernel",
			Type:  "kernel",
		},
	}
	imgDir := t.TempDir()
	for _, img := range imgs {
		path := filepath.Join(imgDir, img.Path)
		dir := filepath.Dir(path)
		if err := os.MkdirAll(dir, os.ModePerm); err != nil {
			t.Fatalf("MkdirAll(%s) failed: %s", dir, err)
		}
		f, err := os.Create(path)
		if err != nil {
			t.Fatalf("Create(%s) failed: %s", path, err)
		}
		defer f.Close()
	}
	return imgs, imgDir
}

func TestAddImageDeps(t *testing.T) {
	imgs, imgDir := mockImages(t)
	testCases := []struct {
		name           string
		pave           bool
		isEmu          bool
		imageOverrides build.ImageOverrides
		deviceType     string
		want           []string
	}{
		{
			name:       "emulator image deps",
			deviceType: "AEMU",
			pave:       false,
			isEmu:      true,
			want:       []string{"fuchsia.zbi", "images.json", "multiboot.bin", "obj/build/images/fuchsia/fuchsia/fvm.blk"},
		},
		{
			name:       "paving image deps",
			deviceType: "NUC",
			pave:       true,
			isEmu:      false,
			want:       []string{"fuchsia.zbi", "images.json", "zedboot.zbi"},
		},
		{
			name:       "netboot image deps",
			deviceType: "NUC",
			pave:       false,
			isEmu:      false,
			want:       []string{"images.json", "netboot.zbi", "zedboot.zbi"},
		},
		{
			name:           "emulator env with image overrides",
			deviceType:     "AEMU",
			pave:           false,
			isEmu:          true,
			imageOverrides: build.ImageOverrides{build.ZbiImage: {Name: "zbi-image"}, build.QemuKernel: {Label: "//:other-qemu-kernel"}},
			want:           []string{"images.json", "other-qemu-kernel", "zbi-image.zbi"},
		},
		{
			name:           "hardware env with image overrides",
			deviceType:     "NUC",
			pave:           false,
			isEmu:          false,
			imageOverrides: build.ImageOverrides{build.ZbiImage: {Name: "zbi-image"}, build.VbmetaImage: {Name: "vbmeta-image"}},
			want:           []string{"images.json", "vbmeta-image.vbmeta", "zbi-image.zbi", "zedboot.zbi"},
		},
		{
			name:       "GCE image deps",
			deviceType: "GCE",
			pave:       false,
			isEmu:      false,
			want:       []string{"images.json"},
		},
		{
			name:  "host-test only shard image deps",
			pave:  false,
			isEmu: false,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			s := &Shard{
				Env: build.Environment{
					IsEmu:          tc.isEmu,
					ImageOverrides: tc.imageOverrides,
					Dimensions: build.DimensionSet{
						DeviceType: tc.deviceType,
					},
				},
			}
			AddImageDeps(s, imgDir, imgs, tc.pave)
			if diff := cmp.Diff(tc.want, s.Deps); diff != "" {
				t.Errorf("AddImageDeps(%v, %v, %t) failed: (-want +got): \n%s", s, imgs, tc.pave, diff)
			}
		})
	}
}
