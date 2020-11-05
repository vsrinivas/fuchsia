// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
	"google.golang.org/protobuf/testing/protocmp"
)

func TestGenerate(t *testing.T) {
	tests := []struct {
		// Name of the test case, also conveniently used as the name of the FVD.
		name    string
		images  imagesJSON
		want    *fvdpb.VirtualDevice
		wantErr bool
	}{{
		name: "ok",
		images: imagesJSON{
			{"qemu-kernel", "/kernel"},
			{"storage-full", "/fvm"},
			{"zircon-a", "/ramdisk"},
		},
		want: &fvdpb.VirtualDevice{
			Name:   "ok",
			Kernel: "qemu-kernel",
			Fvm:    "storage-full",
			Initrd: "zircon-a",
		},
	}, {
		name: "missing kernel",
		images: imagesJSON{
			{"storage-full", "/fvm"},
			{"zircon-a", "/ramdisk"},
		},
		wantErr: true,
	}, {
		name: "missing fvm",
		images: imagesJSON{
			{"qemu-kernel", "/kernel"},
			{"zircon-a", "/ramdisk"},
		},
		wantErr: true,
	}, {
		name: "missing initial ramdisk",
		images: imagesJSON{
			{"qemu-kernel", "/kernel"},
			{"storage-full", "/fvm"},
		},
		wantErr: true,
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := generate(tt.name, tt.images)
			if err != nil != tt.wantErr {
				if tt.wantErr {
					t.Fatalf("got error %v", err)
				}
				t.Fatalf("wanted an error but got %v", got)
			}
			if tt.wantErr {
				return // handled above.
			}
			if diff := cmp.Diff(got, tt.want, protocmp.Transform()); diff != "" {
				t.Fatalf("got diff (+got,-want):\n%s\n", diff)
			}
		})
	}
}
