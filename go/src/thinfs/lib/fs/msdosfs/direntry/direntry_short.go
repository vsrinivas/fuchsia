// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package direntry

import (
	"time"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/bitops"
)

const (
	// DirentrySize is the size of a direntry struct (both long and short versions).
	DirentrySize = 32
)

// The following fields hold values which reside in the "direntry" structure.
const (
	// Special values for "deName[0]":
	charLastFree = 0x00 // This direntry is free (like charFree) and all following entries are free.
	charE5       = 0x05 // The real value is 0xe5, and the entry is NOT free.
	charFree     = 0xe5 // This direntry is free (there is no file/directory name here).

	// Fields of "deAttributes":
	attrNormal    = 0x00                                                // Normal file
	attrReadonly  = 0x01                                                // File is readonly
	attrHidden    = 0x02                                                // File is hidden
	attrSystem    = 0x04                                                // File is a system file
	attrVolume    = 0x08                                                // Entry is a volume label
	attrLongname  = attrReadonly | attrHidden | attrSystem | attrVolume // Long filename
	attrDirectory = 0x10                                                // Entry is a directory name
	attrArchive   = 0x20                                                // File is new or modified
	// The top 2 bits of "deAttributes" are reserved.

	// Format of the deTime fields.
	timeSecondMask  = 0x1F // seconds divided by 2
	timeSecondShift = 0
	timeMinuteMask  = 0x7E0 // minutes
	timeMinuteShift = 5
	timeHourMask    = 0xF800 // hours
	timeHourShift   = 11

	// Format of the deDate fields.
	dateDayMask    = 0x1F // day of month
	dateDayShift   = 0
	dateMonthMask  = 0x1E0 // month
	dateMonthShift = 5
	dateYearMask   = 0xFE00 // year - 1980
	dateYearShift  = 9
)

// Direntry describes an on-disk short directory entry.
type shortDirentry struct {
	dosName         [11]uint8 // Filename, blank filled
	attributes      uint8     // File attributes
	reserved        uint8     // Windows NT specific VFAT lower case flags
	createHundredth uint8     // Hundredth of seconds in CTime (optional)
	createTime      [2]uint8  // Create time (optional)
	createDate      [2]uint8  // Create date (optional)
	accessDate      [2]uint8  // Access date (optional)
	clustHi         [2]uint8  // High bytes of cluster number (0 for FAT12/FAT16)
	writeTime       [2]uint8  // Last update time
	writeDate       [2]uint8  // Last update date
	clustLo         [2]uint8  // Starting cluster of file
	fileSize        [4]uint8  // Size of file in bytes
}

func makeShort(buf []byte) *shortDirentry {
	if len(buf) != DirentrySize {
		panic("Buffer is not the size of a dirent -- cannot read")
	}
	return (*shortDirentry)(unsafe.Pointer(&buf[0]))
}

func (d *shortDirentry) bytes() []byte {
	return (*(*[DirentrySize]byte)(unsafe.Pointer(d)))[:]
}

func (d *shortDirentry) name() string {
	return convertDOSToUnix(d.dosName[:] /* lowercase= */, false)
}

func (d *shortDirentry) nameRaw() []uint8 {
	return d.dosName[:]
}

func (d *shortDirentry) setName(name []uint8) {
	if len(name) != dosNameLen {
		panic("Invalid name length")
	}
	copy(d.dosName[:], name)
}

func (d *shortDirentry) cluster() uint32 {
	return (uint32(bitops.GetLE16(d.clustHi[:])) << 16) | uint32(bitops.GetLE16(d.clustLo[:]))
}

func (d *shortDirentry) setCluster(cluster uint32) {
	bitops.PutLE16(d.clustHi[:], uint16(cluster>>16))
	bitops.PutLE16(d.clustLo[:], uint16(cluster))
}

// lastUpdateTime returns both the time and the date of the last write
func (d *shortDirentry) lastUpdateTime() time.Time {
	wDate := bitops.GetLE16(d.writeDate[:])
	wTime := bitops.GetLE16(d.writeTime[:])

	year := int((wDate&dateYearMask)>>dateYearShift) + 1980 // FAT tracks years starting from 1980
	month := time.Month((wDate & dateMonthMask) >> dateMonthShift)
	day := int((wDate & dateDayMask) >> dateDayShift)
	hour := int((wTime & timeHourMask) >> timeHourShift)
	minute := int((wTime & timeMinuteMask) >> timeMinuteShift)
	twoSecondCount := int((wTime & timeSecondMask) >> timeSecondShift) // Number of seconds / 2
	nsec := 0                                                          // FAT does not measure nanosecond granularity
	location := time.Local

	return time.Date(year, month, day, hour, minute, twoSecondCount*2, nsec, location)
}

// SetLastUpdateTime updates the last modified time
func (d *shortDirentry) setLastUpdateTime(t time.Time) {
	var wDate, wTime uint16
	if t.Year() > 2107 {
		// Unfortunately, FAT stops tracking time after the year 2107. At this point, we'll simply
		// refuse to modify the date.
		t = time.Date(2107, 12, 31, 23, 59, 58, 0, time.Local)
	}

	wDate |= (uint16(t.Year()-1980) << dateYearShift) & dateYearMask
	wDate |= (uint16(t.Month()) << dateMonthShift) & dateMonthMask
	wDate |= (uint16(t.Day()) << dateDayShift) & dateDayMask
	wTime |= (uint16(t.Hour()) << timeHourShift) & timeHourMask
	wTime |= (uint16(t.Minute()) << timeMinuteShift) & timeMinuteMask
	wTime |= (uint16(t.Second()/2) << timeSecondShift) & timeSecondMask

	bitops.PutLE16(d.writeDate[:], wDate)
	bitops.PutLE16(d.writeTime[:], wTime)
}

func (d *shortDirentry) size() uint32 {
	return bitops.GetLE32(d.fileSize[:])
}

func (d *shortDirentry) setSize(size uint32) {
	bitops.PutLE32(d.fileSize[:], size)
}

func (d *shortDirentry) isFree() bool {
	return d.dosName[0] == charFree || d.isLastFree()
}

func (d *shortDirentry) setFree() {
	d.dosName[0] = charFree
}

func (d *shortDirentry) isLastFree() bool {
	return d.dosName[0] == charLastFree
}

func (d *shortDirentry) setLastFree() {
	d.dosName[0] = charLastFree
}
