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
	"errors"
	"fmt"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/util"
)

const (
	bootSig         = 0xAA55
	bootSigExtended = 0x29
	driveNumber     = 0x80
)

// FATType describes the type of underlying filesystem described by the boot record.
type FATType int

// Identify the type of filesystem described by this boot record.
const (
	FATInvalid FATType = 0
	FAT12      FATType = 12
	FAT16      FATType = 16
	FAT32      FATType = 32
)

func bootSectorSignatureValid(sig []uint8) error {
	s := util.GetLE16(sig)
	if s != bootSig {
		return fmt.Errorf("Expected boot signature: %x, but got %x", bootSig, s)
	}
	return nil
}

// Shared beginning of bootsector.
type bootsectorPrefix struct {
	// Boot Sector
	jmpBoot [3]uint8 // Jump instruction 0xEB__90 or 0xE9____
	oemName [8]uint8 // Informal name of system which formatted the volume
}

func (b *bootsectorPrefix) validate() error {
	if b.jmpBoot[0] == 0xE9 {
		return nil
	}
	if b.jmpBoot[0] == 0xEB && b.jmpBoot[2] == 0x90 {
		return nil
	}
	return errors.New("Invalid jmpBoot instruction")
}

// Shared BPB fields between FAT12/16/32.
type bpbShared struct {
	bytesPerSec        [2]uint8 // 512, 1024, 2048, or 4096
	sectorsPerCluster  uint8    // Must be a power of 2 between 1 and 128
	numSectorsReserved [2]uint8 // FAT12/16: 1. FAT32: 32
	numFATs            uint8    // Greater than or equal to 1. Usually 2
	numRootEntries     [2]uint8 // FAT12/16: Root directory entries. FAT32: 0
	totalSectors16     [2]uint8 // FAT12/16: Sector count, if < 0x10000. FAT32: Always 0
	media              uint8    // Media descriptor
	sectorsPerFAT16    [2]uint8 // FAT12/16: Sectors for ONE FAT. FAT32: 0
	sectorsPerTrack    [2]uint8 // Geometry info
	numHeads           [2]uint8 // Geometry info
	numSectorsHidden   [4]uint8 // Hidden sectors preceding partition
	totalSectors32     [4]uint8 // FAT12/16: Sector count if >= 0x10000. FAT32: Sector count
}

func (b *bpbShared) BytesPerSec() uint32 {
	return uint32(util.GetLE16(b.bytesPerSec[:]))
}
func (b *bpbShared) SectorsPerCluster() uint32 {
	return uint32(b.sectorsPerCluster)
}
func (b *bpbShared) NumSectorsReserved() uint32 {
	return uint32(util.GetLE16(b.numSectorsReserved[:]))
}
func (b *bpbShared) NumFATs() uint32 {
	return uint32(b.numFATs)
}
func (b *bpbShared) NumRootEntries() uint32 {
	return uint32(util.GetLE16(b.numRootEntries[:]))
}
func (b *bpbShared) TotalSectors16() uint32 {
	return uint32(util.GetLE16(b.totalSectors16[:]))
}
func (b *bpbShared) SectorsPerFAT16() uint32 {
	return uint32(util.GetLE16(b.sectorsPerFAT16[:]))
}
func (b *bpbShared) SectorsPerTrack() uint32 {
	return uint32(util.GetLE16(b.sectorsPerTrack[:]))
}
func (b *bpbShared) NumHeads() uint32 {
	return uint32(util.GetLE16(b.numHeads[:]))
}
func (b *bpbShared) SectorsHidden() uint32 {
	return uint32(util.GetLE32(b.numSectorsHidden[:]))
}
func (b *bpbShared) TotalSectors32() uint32 {
	return uint32(util.GetLE32(b.totalSectors32[:]))
}
func (b *bpbShared) TotalSectors() uint32 {
	ts16 := b.TotalSectors16()
	ts32 := b.TotalSectors32()
	if ts16 != 0 {
		return ts16
	}
	return ts32
}

// Without verification, tries to guess FAT group. Returns true for FAT32, false for FAT12/16.
// Does not verify any correctness.
func (b *bpbShared) guessFATType() bool {
	return b.NumRootEntries() == 0
}

func (b *bpbShared) TotalClusters(firstDataSector uint32) uint32 {
	totalSectors := b.TotalSectors()
	sectorsPerCluster := b.SectorsPerCluster()
	numDataSectors := totalSectors - firstDataSector
	return numDataSectors / sectorsPerCluster
}

// For a detailed description of the validation occurring here, see pages 9-13 of "FAT: General
// Overview of On-Disk Format".
// The "large" argument is true if the filesystem supported by bpbShared is FAT32.
// "large" is false for FAT12 and FAT16.
func (b *bpbShared) validate(large bool) error {
	bytesPerSec := b.BytesPerSec()
	switch bytesPerSec {
	case 512, 1024, 2048, 4096:
	default:
		return fmt.Errorf("Bytes/Sector invalid: %d", bytesPerSec)
	}

	sectorsPerCluster := uint32(b.sectorsPerCluster)
	switch sectorsPerCluster {
	case 1, 2, 4, 8, 16, 32, 64, 128:
	default:
		return fmt.Errorf("Sectors/Cluster invalid: %d", sectorsPerCluster)
	}

	// Validation of both bytesPerSec and sectorsPerCluster together.
	bytesPerCluster := bytesPerSec * sectorsPerCluster
	if bytesPerCluster > 32*1024 {
		return fmt.Errorf("Byte/Cluster greater than 32K: %d", bytesPerCluster)
	}

	numSectorsReserved := b.NumSectorsReserved()
	if large && numSectorsReserved == 0 {
		return errors.New("FAT32 cannot have zero reserved sectors")
	} else if !large && numSectorsReserved != 1 {
		return errors.New("FAT12/16 cannot have any number of reserved sectors other than one")
	}

	if b.NumFATs() == 0 {
		return errors.New("NumFATs cannot be zero")
	}

	numRootEntries := b.NumRootEntries()
	if large && numRootEntries != 0 {
		return errors.New("FAT32 must set numRootEntries to zero")
	} else if !large {
		if numRootEntries == 0 {
			return errors.New("FAT12/16 must have at least one root entry")
		} else if numRootEntries*32%bytesPerSec != 0 {
			// "For FAT12 and FAT16 volumes, this value should always specify a count that when
			// multiplied by 32 results in an even multiple of BPB_BytsPerSec".
			return errors.New("NumRootEntries * 32 must be an even multiple of bytes per sector")
		}
	}

	// totalSectors16 and totalSectors32 should be evaluated together.
	totalSectors16 := b.TotalSectors16()
	totalSectors32 := b.TotalSectors32()
	if large {
		if totalSectors16 != 0 {
			return errors.New("FAT32 must set totalSectors16 to zero")
		} else if totalSectors32 == 0 {
			return errors.New("FAT32 must set totalSectors32 to something other than zero")
		}
	} else {
		if totalSectors16 == 0 && totalSectors32 == 0 {
			return errors.New("FAT12/16 must set either totalSectors16 or totalSectors32, not neither")
		} else if totalSectors16 != 0 && totalSectors32 != 0 {
			return errors.New("FAT12/16 must set at most one of totalSectors16 or totalSectors32, not both")
		}
	}

	sectorsPerFAT16 := b.SectorsPerFAT16()
	if large && sectorsPerFAT16 != 0 {
		return errors.New("FAT32 must set sectorsPerFAT16 to zero")
	} else if !large && sectorsPerFAT16 == 0 {
		return errors.New("FAT12/16 must set sectorsPerFAT16 to something other than zero")
	}
	return nil
}

// Shared bootsector fields between FAT12/16 and FAT32, but at different offsets.
type bootsectorSuffix struct {
	driveNumber uint8     // Drive Number: 0x80
	reserved1   uint8     // Zero
	extBootSig  uint8     // bootSigExtended
	volumeID    [4]uint8  // Volume Serial Number (usually related to creation time)
	volumeLabel [11]uint8 // Volume Label
	fsType      [8]uint8  // FS Type (either FAT12, FAT16 or FAT32)
}

func (b *bootsectorSuffix) validate() error {
	if b.driveNumber != driveNumber {
		return fmt.Errorf("Expected drive number %x, found: %x", driveNumber, b.driveNumber)
	}
	if b.extBootSig != bootSigExtended {
		return fmt.Errorf("Expected boot signature: %x, found %x", bootSigExtended, b.extBootSig)
	}

	return nil
}
