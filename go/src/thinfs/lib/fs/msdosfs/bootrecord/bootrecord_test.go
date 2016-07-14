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

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/testutil"
)

func TestClusterSize(t *testing.T) {
	sectorSize := 512
	sectorsPerCluster := 4
	clusterSize := uint32(sectorSize * sectorsPerCluster)
	fat := testutil.MkfsFAT(t, "1G", 2, 0, sectorsPerCluster, sectorSize)
	d := fat.GetDevice()

	br, err := New(d)
	if err != nil {
		t.Fatal(err)
	}

	actualClusterSize := br.ClusterSize()
	if actualClusterSize != clusterSize {
		t.Fatalf("Unexpected cluster size %d (expected %d)", actualClusterSize, clusterSize)
	}

	d.Close()
	fat.RmfsFAT()
}

func TestEntrySize(t *testing.T) {
	doTest := func(size string, entrySize uint32) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		br, err := New(d)
		if err != nil {
			t.Fatal(err)
		}

		actualEntrySize := br.FATEntrySize()
		if actualEntrySize != entrySize {
			t.Fatalf("Unexpected entry size %d (expected %d)", actualEntrySize, entrySize)
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest("1G", 4)
	doTest("10M", 2)
}

// Test the two valid configurations for the boot jump instructions.
// Also test an invalid configuration.
func TestJmpBoot(t *testing.T) {
	doTest := func(size string) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{0xE9}, 0) // Valid
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.WriteAt([]byte{0xEB}, 0) // Valid
		d.WriteAt([]byte{0x90}, 2)
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.WriteAt([]byte{0xAA}, 0) // Invalid
		if _, err := New(d); err == nil {
			t.Fatal("Expected error with invalid Jmp Boot")
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest("1G")
	doTest("10M")
}

// Test valid and invalid signatures at the end of the boot sector.
func TestSectorSignature(t *testing.T) {
	doTest := func(size string) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{0x55}, 510) // Valid
		d.WriteAt([]byte{0xAA}, 511) // Valid
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.WriteAt([]byte{0x00}, 510) // Invalid
		d.WriteAt([]byte{0x11}, 511)
		if _, err := New(d); err == nil {
			t.Fatal("Expected error with invalid sector signature")
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest("1G")
	doTest("10M")
}

// Test both sector size and cluster sizes. They are correlated, and must be tested together.
func TestBytesPerSectorAndSectorsPerCluster(t *testing.T) {
	expectSuccess := func(size string, bytesPerSector uint16, sectorsPerCluster uint8) {
		fat := testutil.MkfsFAT(t, size, 2, 0, int(sectorsPerCluster), int(bytesPerSector))
		d := fat.GetDevice()
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, bytesPerSector uint16, sectorsPerCluster uint8) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		// MKFS may not cooperate with an invalid value. We'll change the device manually.
		d.WriteAt([]byte{uint8(bytesPerSector)}, 11)
		d.WriteAt([]byte{uint8(bytesPerSector >> 8)}, 12)
		d.WriteAt([]byte{sectorsPerCluster}, 13)
		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, sectorsPerCluster: %d", size, sectorsPerCluster)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectSuccess("1G", 512, 1)
	expectSuccess("10M", 512, 1)
	expectSuccess("1G", 512, 2)
	expectSuccess("10M", 512, 2)
	expectSuccess("1G", 512, 4)
	expectSuccess("10M", 512, 4)
	expectSuccess("1G", 512, 8)
	expectSuccess("1G", 512, 16)
	expectSuccess("2G", 512, 32)
	expectSuccess("3G", 512, 64)  // Maximum possible combination; 32K
	expectSuccess("3G", 1024, 32) // Maximum possible combination; 32K
	expectSuccess("3G", 2048, 16) // Maximum possible combination; 32K
	expectSuccess("3G", 4096, 8)  // Maximum possible combination; 32K

	expectFailure("1G", 256, 1)   // Bytes per sector must be at least 256
	expectFailure("10M", 256, 1)  // Bytes per sector must be at least 256
	expectFailure("1G", 513, 1)   // Bytes per sector must be a power of 2
	expectFailure("1G", 512, 0)   // Sectors/cluster must be greater than zero
	expectFailure("1G", 512, 3)   // Sectors/cluster must be a power of 2
	expectFailure("3G", 1024, 64) // Larger than 32K
	expectFailure("3G", 512, 128) // Larger than 32K
}

func TestReservedSectorCount(t *testing.T) {
	expectSuccess := func(size string, reservedSectorCount int) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		// MKFS may not cooperate with an invalid value. We'll change the device manually.
		d.WriteAt([]byte{uint8(reservedSectorCount)}, 14)
		d.WriteAt([]byte{uint8(reservedSectorCount >> 8)}, 15)
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, reservedSectorCount uint16) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		// MKFS may not cooperate with an invalid value. We'll change the device manually.
		d.WriteAt([]byte{uint8(reservedSectorCount)}, 14)
		d.WriteAt([]byte{uint8(reservedSectorCount >> 8)}, 15)
		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, reservedSectorCount: %d", size, reservedSectorCount)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectSuccess("1G", 30)
	expectSuccess("10M", 1)

	expectFailure("1G", 0)
	expectFailure("1G", 2) // Not enough room for Boot sector + FS Info + Backup sector
	expectFailure("10M", 0)
	expectFailure("10M", 2)
}

func TestNumFATs(t *testing.T) {
	expectSuccess := func(size string, numFATs int) {
		fat := testutil.MkfsFAT(t, size, numFATs, 0, 4, 512)
		d := fat.GetDevice()
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, numFATs uint8) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		// MKFS may not cooperate with an invalid value. We'll change the device manually.
		d.WriteAt([]byte{numFATs}, 16)
		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, numFATs: %d", size, numFATs)
		}
		d.Close()
		fat.RmfsFAT()
	}

	expectSuccess("1G", 2)
	expectSuccess("10M", 2)

	expectFailure("1G", 0)
	expectFailure("10M", 0)
}

func TestRootEntryCount(t *testing.T) {
	expectSuccess := func(size string, rootEntryCount uint16) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(rootEntryCount)}, 17)
		d.WriteAt([]byte{uint8(rootEntryCount >> 8)}, 18)
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, rootEntryCount uint16) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(rootEntryCount)}, 17)
		d.WriteAt([]byte{uint8(rootEntryCount >> 8)}, 18)
		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, rootEntryCount: %d", size, rootEntryCount)
		}

		d.Close()
		fat.RmfsFAT()
	}

	expectSuccess("1G", 0) // Must always be zero for FAT32
	expectFailure("1G", 1)

	expectSuccess("10M", 512) // Must be an even multiple of sector size (512 in this case) for FAT16
	expectSuccess("10M", 1024)
	expectSuccess("10M", 2048)

	expectFailure("10M", 0)
	expectFailure("10M", 1)
	expectFailure("10M", 513)
	expectFailure("10M", 1023)
}

func TestTotalSectorCount(t *testing.T) {
	expectSuccess := func(size string, totalSectors16 uint16, totalSectors32 uint32) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(totalSectors16)}, 19)
		d.WriteAt([]byte{uint8(totalSectors16 >> 8)}, 20)

		d.WriteAt([]byte{uint8(totalSectors32)}, 32)
		d.WriteAt([]byte{uint8(totalSectors32 >> 8)}, 33)
		d.WriteAt([]byte{uint8(totalSectors32 >> 16)}, 34)
		d.WriteAt([]byte{uint8(totalSectors32 >> 24)}, 35)

		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, totalSectors16 uint16, totalSectors32 uint32) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(totalSectors16)}, 19)
		d.WriteAt([]byte{uint8(totalSectors16 >> 8)}, 20)

		d.WriteAt([]byte{uint8(totalSectors32)}, 32)
		d.WriteAt([]byte{uint8(totalSectors32 >> 8)}, 33)
		d.WriteAt([]byte{uint8(totalSectors32 >> 16)}, 34)
		d.WriteAt([]byte{uint8(totalSectors32 >> 24)}, 35)

		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, totalSectors 16 / 32: %d / %d", size, totalSectors16, totalSectors32)
		}

		d.Close()
		fat.RmfsFAT()
	}

	// FAT32
	expectSuccess("1G", 0, 0x100000)
	expectFailure("1G", 0, 0)             // Must set sectors32
	expectFailure("1G", 0x1000, 0)        // Cannot set sectors16
	expectFailure("1G", 0x1000, 0x100000) // Cannot set sectors16

	// FAT16
	expectSuccess("10M", 0x5000, 0)
	expectSuccess("10M", 0, 0x5000)
	expectFailure("10M", 0x5000, 0x5000)  // Cannot set both sectors16 and sectors32
	expectFailure("10M", 0x5000, 0x10000) // Cannot set both sectors16 and sectors32
	expectFailure("10M", 0, 0)            // Must set at least one of them
}

func TestSectorsPerFAT16(t *testing.T) {
	expectSuccess := func(size string, sectorsPerFAT16 uint16) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(sectorsPerFAT16)}, 22)
		d.WriteAt([]byte{uint8(sectorsPerFAT16 >> 8)}, 23)
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.Close()
		fat.RmfsFAT()
	}

	expectFailure := func(size string, sectorsPerFAT16 uint16) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{uint8(sectorsPerFAT16)}, 22)
		d.WriteAt([]byte{uint8(sectorsPerFAT16 >> 8)}, 23)
		if _, err := New(d); err == nil {
			t.Fatalf("Expected failure for size: %s, sectorsPerFAT16: %d", size, sectorsPerFAT16)
		}

		d.Close()
		fat.RmfsFAT()
	}

	// Must be unset for FAT32
	expectSuccess("1G", 0)
	expectFailure("1G", 1)

	// Must be set for FAT16
	expectSuccess("10M", 1)
	expectFailure("10M", 0)
}

func TestDriveNumber(t *testing.T) {
	doTest := func(size string, offset int64) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{0x80}, offset) // Valid
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.WriteAt([]byte{0x81}, offset) // Invalid
		if _, err := New(d); err == nil {
			t.Fatal("Expected error with invalid drive number")
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest("10M", 36)
	doTest("1G", 64)
}

func TestBootSignature(t *testing.T) {
	doTest := func(size string, offset int64) {
		fat := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
		d := fat.GetDevice()

		d.WriteAt([]byte{0x29}, offset) // Valid
		if _, err := New(d); err != nil {
			t.Fatal(err)
		}

		d.WriteAt([]byte{0x30}, offset) // Invalid
		if _, err := New(d); err == nil {
			t.Fatal("Expected error with invalid drive number")
		}

		d.Close()
		fat.RmfsFAT()
	}

	doTest("10M", 38)
	doTest("1G", 66)
}
