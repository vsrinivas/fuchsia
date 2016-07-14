// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package bootrecord

import (
	"testing"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/testutil"
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
