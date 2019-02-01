// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootrecord

import (
	"testing"
	"unsafe"

	"thinfs/fs/msdosfs/testutil"
)

func TestBootrecordSmallSize(t *testing.T) {
	br := &brSmall{}
	size := unsafe.Sizeof(*br)
	if size != BootrecordSize {
		t.Fatalf("Expected brSmall size: %d, but got %d", BootrecordSize, size)
	}
	align := unsafe.Alignof(*br)
	if align != 1 {
		t.Fatalf("Expected brSmall to have no alignment requirements, but Alignof is: %d", align)
	}
}

func TestValidFAT12(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1M", 2, 0, 4, 512)
	defer fat.RmfsFAT()
	d := fat.GetDevice()
	defer d.Close()

	if _, err := New(d); err == nil {
		t.Fatal("FAT12 should be disallowed")
	}
}

func TestValidFAT16(t *testing.T) {
	fat := testutil.MkfsFAT(t, "10M", 2, 0, 4, 512)
	defer fat.RmfsFAT()
	d := fat.GetDevice()
	defer d.Close()

	br, err := New(d)
	if err != nil {
		t.Fatal(err)
	}
	if br.Type() != FAT16 {
		t.Fatal("FAT type was not FAT16, but it should have been")
	}
}
