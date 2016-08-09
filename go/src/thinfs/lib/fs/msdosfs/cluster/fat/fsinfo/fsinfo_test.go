// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fsinfo

import (
	"testing"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/testutil"
)

// Tests the size of the FS Info structure
func TestFsInfoSize(t *testing.T) {
	if sz := unsafe.Sizeof(fsInfo{}); sz != 512 {
		t.Fatalf("fsInfo struct is %d byte, but it should be 512 bytes", sz)
	}
	info := &fsInfo{}
	align := unsafe.Alignof(*info)
	if align != 1 {
		t.Fatalf("Expected fs info to have no alignment requirements, but Alignof is: %d", align)
	}
	fcOffset := unsafe.Offsetof(info.freeCountClusters)
	if freeCountOffset != fcOffset {
		t.Fatalf("Expected freeCountOffset of %d, but saw %d\n", freeCountOffset, fcOffset)
	}
	nfOffset := unsafe.Offsetof(info.nextFreeCluster)
	if nextFreeClusterOffset != nfOffset {
		t.Fatalf("Expected nextFreeClusterOffset of %d, but saw %d\n", nextFreeClusterOffset, nfOffset)
	}
}

// Tests the FS Info structure can be read and written to.
func TestFsInfoFAT32ReadWrite(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	d := fs.GetDevice()
	defer d.Close()

	// Read the bootrecord, get the offset of the FS Info
	br, err := bootrecord.New(d)
	if err != nil {
		t.Fatal(err)
	}
	offset, err := br.FsInfoOffset()
	if err != nil {
		t.Fatal(err)
	}

	// Read the default hints
	freeCount, nextFree, err := ReadHints(d, offset)

	if err != nil {
		t.Fatal(err)
	} else if freeCount == 0 || freeCount == UnknownFlag {
		t.Fatalf("Unexpected free count: %d", freeCount)
	} else if nextFree != 2 {
		t.Fatalf("Unexpected next free: %d (expected %d)", nextFree, 2)
	}

	// Write new hints
	newFreeCount := uint32(10)
	newNextFree := uint32(15)
	if err := WriteFreeCount(d, offset, newFreeCount); err != nil {
		t.Fatal(err)
	} else if err := WriteNextFree(d, offset, newNextFree); err != nil {
		t.Fatal(err)
	}

	// Check the hints we just wrote actually ended up in the right place
	freeCount, nextFree, err = ReadHints(d, offset)
	if err != nil {
		t.Fatal(err)
	} else if freeCount != newFreeCount {
		t.Fatalf("Unexpected free count: %d (expected %d)", freeCount, newFreeCount)
	} else if nextFree != newNextFree {
		t.Fatalf("Unexpected next free: %d (expected %d)", nextFree, newNextFree)
	}

	// Write an unknown "next free" hint.
	if err := WriteNextFree(d, offset, 0xFFFFFFFF); err != nil {
		t.Fatal(err)
	} else if _, _, err = ReadHints(d, offset); err == nil {
		t.Fatal("Expected that an unknown 'next free' hint would cause an error")
	}

	// Put the "next free" hint back to a valid value, and then repeat the process with an invalid
	// "free count" hint.
	if err := WriteNextFree(d, offset, newNextFree); err != nil {
		t.Fatal(err)
	} else if err := WriteFreeCount(d, offset, 0xFFFFFFFF); err != nil {
		t.Fatal(err)
	} else if _, _, err = ReadHints(d, offset); err == nil {
		t.Fatal("Expected that an unknown 'free count' hint would cause an error")
	}

}

// Tests the FS Info structure validates signatures on FAT32
func TestFsInfoFAT32Invalid(t *testing.T) {
	doTest := func(byteToModify int64, newVal byte) {
		fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
		d := fs.GetDevice()

		// Read the bootrecord, get the offset of the FS Info
		br, err := bootrecord.New(d)
		if err != nil {
			t.Fatal(err)
		}
		offset, err := br.FsInfoOffset()
		if err != nil {
			t.Fatal(err)
		}

		// Mess up the fsInfo signature (make it invalid)
		if _, err := d.WriteAt([]byte{0}, offset+byteToModify); err != nil {
			t.Fatal(err)
		}

		if _, _, err := ReadHints(d, offset); err == nil {
			t.Fatalf("Expected ReadHints to fail with modified byte at %d", byteToModify)
		}

		d.Close()
		fs.RmfsFAT()
	}

	doTest(0, 0)   // Mess up leading signature
	doTest(484, 0) // Mess up inner signature
	doTest(511, 0) // Mess up final signature
}

// Tests the FS Info structure is not accessible on FAT16
func TestFsInfoFAT16Invalid(t *testing.T) {
	fs := testutil.MkfsFAT(t, "10M", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	d := fs.GetDevice()
	defer d.Close()

	// Read the bootrecord, get the offset of the FS Info
	br, err := bootrecord.New(d)
	if err != nil {
		t.Fatal(err)
	}
	_, err = br.FsInfoOffset()
	if err == nil {
		t.Fatal("Expected FS Info offset to fail for FAT 16")
	}
}
