// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build"
)

func mockImages(t *testing.T) []build.Image {
	t.Helper()
	return []build.Image{
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
			Name: "other-qemu-kernel",
			Path: "other-qemu-kernel",
			Type: "kernel",
		},
	}
}

func TestAddImageDeps(t *testing.T) {
	imgs := mockImages(t)
	testCases := []struct {
		name           string
		pave           bool
		isEmu          bool
		imageOverrides build.ImageOverrides
		want           []string
	}{
		{
			name:  "emulator image deps",
			pave:  false,
			isEmu: true,
			want:  []string{"fuchsia.zbi", "images.json", "multiboot.bin", "obj/build/images/fuchsia/fuchsia/fvm.blk"},
		},
		{
			name:  "paving image deps",
			pave:  true,
			isEmu: false,
			want:  []string{"fuchsia.zbi", "images.json", "zedboot.zbi"},
		},
		{
			name:  "netboot image deps",
			pave:  false,
			isEmu: false,
			want:  []string{"images.json", "netboot.zbi", "zedboot.zbi"},
		},
		{
			name:           "emulator env with image overrides",
			pave:           false,
			isEmu:          true,
			imageOverrides: build.ImageOverrides{build.ZbiImage: "zbi-image", build.QemuKernel: "other-qemu-kernel"},
			want:           []string{"images.json", "other-qemu-kernel", "zbi-image.zbi"},
		},
		{
			name:           "hardware env with image overrides",
			pave:           false,
			isEmu:          false,
			imageOverrides: build.ImageOverrides{build.ZbiImage: "zbi-image", build.VbmetaImage: "vbmeta-image"},
			want:           []string{"images.json", "vbmeta-image.vbmeta", "zbi-image.zbi", "zedboot.zbi"},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			s := &Shard{
				Env: build.Environment{
					IsEmu:          tc.isEmu,
					ImageOverrides: tc.imageOverrides,
				},
			}
			AddImageDeps(s, imgs, tc.pave)
			if diff := cmp.Diff(tc.want, s.Deps); diff != "" {
				t.Errorf("AddImageDeps(%v, %v, %t) failed: (-want +got): \n%s", s, imgs, tc.pave, diff)
			}
		})
	}
}
