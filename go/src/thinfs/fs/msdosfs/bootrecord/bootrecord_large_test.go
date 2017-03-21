// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootrecord

import (
	"testing"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/fs/msdosfs/testutil"
)

func TestBootrecordLargeSize(t *testing.T) {
	br := &brLarge{}
	size := unsafe.Sizeof(*br)
	if size != BootrecordSize {
		t.Fatalf("Expected brLarge size: %d, but got %d", BootrecordSize, size)
	}
	align := unsafe.Alignof(*br)
	if align != 1 {
		t.Fatalf("Expected brLarge to have no alignment requirements, but Alignof is: %d", align)
	}
}

func TestValidFAT32(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	d := fat.GetDevice()

	// Read the bootrecord into buf.
	br, err := New(d)
	if err != nil {
		t.Fatal(err)
	} else if br.Type() != FAT32 {
		t.Fatal("FAT type was not FAT32, but it should have been")
	}

	volumeSize := br.VolumeSize()
	expectedVolumeSize := int64(1 << 30) // 1 GB
	if volumeSize != expectedVolumeSize {
		t.Fatalf("Expected volume size to be %d, but it was %d", expectedVolumeSize, volumeSize)
	}

	active, numFATs, primary := br.MirroringInfo()
	if !active {
		t.Fatal("Expected mirroring to be active")
	} else if numFATs != 2 {
		t.Fatalf("Expected numFATs to be 2, but it was %d", numFATs)
	} else if primary != 0 {
		t.Fatalf("Expected primary to be 0, but it was %d", primary)
	}

	numUsableClusters := br.NumUsableClusters()

	expectedReservedSectors := int64(32)
	expectedSectorSize := int64(512)
	expectedReserveClusters := int64(2)
	expectedSectorsPerCluster := int64(4)
	expectedSectorsPerFAT := int64(4081)
	expectedNumUsableBytes := volumeSize
	expectedNumUsableBytes -= expectedReservedSectors * expectedSectorSize
	expectedNumUsableBytes -= int64(numFATs) * expectedSectorsPerFAT * expectedSectorSize
	expectedNumUsableClusters := (expectedNumUsableBytes / (expectedSectorSize * expectedSectorsPerCluster)) - expectedReserveClusters

	if numUsableClusters != uint32(expectedNumUsableClusters) {
		t.Fatalf("Expected numUsableClusters to be %d, but it was %d", expectedNumUsableClusters, numUsableClusters)
	}

	d.Close()
	fat.RmfsFAT()
}

func TestInvalidFAT32SectorsPerFAT32(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	d := fat.GetDevice()

	d.WriteAt([]byte{0}, 36)
	d.WriteAt([]byte{0}, 37)
	d.WriteAt([]byte{0}, 38)
	d.WriteAt([]byte{0}, 39)
	if _, err := New(d); err == nil {
		t.Fatalf("Expected zero value for Sectors Per FAT32 to fail")
	}

	d.Close()
	fat.RmfsFAT()
}

func TestInvalidFAT32RootCluster(t *testing.T) {
	doTest := func(success bool, rootCluster int) {
		fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(rootCluster)}, 44)
		d.WriteAt([]byte{uint8(rootCluster >> 8)}, 45)
		d.WriteAt([]byte{uint8(rootCluster >> 16)}, 46)
		d.WriteAt([]byte{uint8(rootCluster >> 24)}, 47)

		if success {
			if _, err := New(d); err != nil {
				t.Fatal(err)
			}
		} else {
			if _, err := New(d); err == nil {
				t.Fatalf("Expected root cluster value %d to fail", rootCluster)
			}
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest(true, 2)
	doTest(true, 3)

	doTest(false, 0)
	doTest(false, 1)
	doTest(false, 0x100000)
}

func TestInvalidBackupBootSector(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	d := fat.GetDevice()

	d.WriteAt([]byte{0}, 50)
	d.WriteAt([]byte{0}, 51)
	if _, err := New(d); err == nil {
		t.Fatalf("Expected backup boot sector of zero to fail")
	}

	d.WriteAt([]byte{50}, 50)
	d.WriteAt([]byte{0}, 51)
	if _, err := New(d); err == nil {
		t.Fatalf("Expected backup boot sector larger than the number of reserved sectors to fail")
	}

	d.Close()
	fat.RmfsFAT()
}

func TestInvalidFSInfoSector(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	d := fat.GetDevice()

	d.WriteAt([]byte{0}, 48)
	d.WriteAt([]byte{0}, 49)
	if _, err := New(d); err == nil {
		t.Fatalf("Expected fs info sector of zero to fail")
	}

	d.WriteAt([]byte{50}, 48)
	d.WriteAt([]byte{0}, 49)
	if _, err := New(d); err == nil {
		t.Fatalf("Expected fs info sector larger than the number of reserved sectors to fail")
	}

	d.Close()
	fat.RmfsFAT()
}

func TestInvalidMirroringInfo(t *testing.T) {
	fat := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	d := fat.GetDevice()

	// The 40th byte in the FAT32 boot sector contains the following information:
	//	M _ _ _ F F F F
	// 	7 6 5 4 3 2 1 0
	// 	M: 0 if mirroring to all FATs, else 1.
	// 	F: Number of primary FAT if, mirroring is disabled.

	// Primary FAT index 1 (valid, there are two FATs)
	d.WriteAt([]byte{0x81}, 40)
	if _, err := New(d); err != nil {
		t.Fatal(err)
	}
	// Primary FAT index 2 (out of bounds)
	d.WriteAt([]byte{0x82}, 40)
	if _, err := New(d); err == nil {
		t.Fatal("Expected error relating to invalid primary FAT")
	}

	d.Close()
	fat.RmfsFAT()

}
