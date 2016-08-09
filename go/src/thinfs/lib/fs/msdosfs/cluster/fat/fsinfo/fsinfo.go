// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package fsinfo describes the FAT32-exclusive FSInfo structure
package fsinfo

import (
	"errors"
	"fmt"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bits"
	"fuchsia.googlesource.com/thinfs/lib/thinio"
)

const (
	freeCountOffset       = 488
	nextFreeClusterOffset = 492

	leadSig  = 0x41615252
	innerSig = 0x61417272
	finalSig = 0xAA550000

	// UnknownFlag describes a value (such as the next free cluster) which is not known.
	UnknownFlag = 0xFFFFFFFF
)

// fsInfo describes the FAT32 FSInfo block.
type fsInfo struct {
	leadSignature     [4]uint8   // 0x41615252
	reserved1         [480]uint8 // Zero
	innerSignature    [4]uint8   // 0x61417272
	freeCountClusters [4]uint8   // The last known count of free clusters or 0xFFFFFFFF
	nextFreeCluster   [4]uint8   // A hint for the FAT driver or 0xFFFFFFFF
	reserved2         [12]uint8  // Zero
	finalSignature    [4]uint8   // 0xAA550000
}

// Validate verifies the FsInfo is valid.
func (f *fsInfo) validate() error {
	// Validate signatures
	s := bits.GetLE32(f.leadSignature[:])
	if s != leadSig {
		return fmt.Errorf("Expected leading signature: %d, but got %d", leadSig, s)
	}
	s = bits.GetLE32(f.innerSignature[:])
	if s != innerSig {
		return fmt.Errorf("Expected inner signature: %d, but got %d", innerSig, s)
	}
	s = bits.GetLE32(f.finalSignature[:])
	if s != finalSig {
		return fmt.Errorf("Expected final signature: %d, but got %d", finalSig, s)
	}

	return nil
}

// ReadHints reads FS Info from the device (at the offset) and returns the freeCount of clusters and the
// nextFree cluster (if they are known). Otherwise, returns an error.
func ReadHints(d *thinio.Conductor, offset int64) (freeCount, nextFree uint32, err error) {
	var info fsInfo
	b := (*(*[unsafe.Sizeof(info)]byte)(unsafe.Pointer(&info)))[:]

	_, err = d.ReadAt(b, offset)
	if err != nil {
		return 0, 0, err
	}

	if err = info.validate(); err != nil {
		return 0, 0, err
	}

	freeCount = bits.GetLE32(info.freeCountClusters[:])
	nextFree = bits.GetLE32(info.nextFreeCluster[:])
	if freeCount == UnknownFlag {
		return 0, 0, errors.New("Unknown free count of clusters")
	} else if nextFree == UnknownFlag {
		return 0, 0, errors.New("Unknown next free cluster")
	}
	return
}

// WriteFreeCount takes the offset of the FS Info structure and updates the on-disk value of the
// "free cluster count".
func WriteFreeCount(d *thinio.Conductor, offset int64, freeCount uint32) error {
	b := make([]byte, 4)
	bits.PutLE32(b, freeCount)
	_, err := d.WriteAt(b, offset+freeCountOffset)
	return err
}

// WriteNextFree takes the offset of the FS Info structure and updates the on-disk value of the
// "next free cluster".
func WriteNextFree(d *thinio.Conductor, offset int64, nextFree uint32) error {
	b := make([]byte, 4)
	bits.PutLE32(b, nextFree)
	_, err := d.WriteAt(b, offset+nextFreeClusterOffset)
	return err
}
