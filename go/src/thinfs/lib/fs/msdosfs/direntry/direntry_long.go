// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package direntry

import (
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/bitops"
)

const (
	longDirentLen     = 13                                                   // Number of chars per longDirentry
	longnameMaxLen    = 255                                                  // Maximum filename length in Win95.
	maxLongDirentries = (longnameMaxLen + longDirentLen - 1) / longDirentLen // Maximum number of winentries for a filename

	// Flags for "deLongCnt":
	longLastEntry   = 0x40 // Used to indicate this is the last longDirentry entry in a set
	longOrdinalMask = 0x3f // Flag used to find the order of this longDirentry entry
)

// longDirentry is a Win95 long name directory entry. Stored on disk.
// Note: The long name components are stored as UCS-2; they always use two bytes per character.
type longDirentry struct {
	count      uint8     // Order of entry in sequence of multiple direntLong entries
	name1      [10]uint8 // Characters 1-5 of the long-name subcomponent
	attributes uint8     // Must be "attrLongname"
	reserved1  uint8
	chksum     uint8     // Checksum of the name in the short dir entry at the end of the sequence
	name2      [12]uint8 // Characters 6-11 of the long-name subcomponent
	reserved2  [2]uint8
	name3      [4]uint8 // Characters 12-13 of the long-name subcomponent
}

func makeLong(buf []byte) *longDirentry {
	if len(buf) != DirentrySize {
		panic("Buffer is not the size of a dirent -- cannot read")
	}
	return (*longDirentry)(unsafe.Pointer(&buf[0]))
}

// Gets the raw name of the long direntry component, including NULL and padding, if any
func (d *longDirentry) nameRaw() []uint16 {
	nameBuffer := make([]uint16, 0, longDirentLen)
	writeNamePartToBuffer := func(part []uint8) {
		for i := 0; i < len(part); i += 2 {
			charWin := bitops.GetLE16(part[i : i+2])
			nameBuffer = append(nameBuffer, charWin)
		}
	}
	writeNamePartToBuffer(d.name1[:])
	writeNamePartToBuffer(d.name2[:])
	writeNamePartToBuffer(d.name3[:])
	return nameBuffer
}

func (d *longDirentry) bytes() []byte {
	return (*(*[DirentrySize]byte)(unsafe.Pointer(d)))[:]
}
