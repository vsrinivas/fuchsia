// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package fat contains the actual File Allocation Table used by the FAT filesystem.
//
// Methods which modify the FAT itself are NOT thread-safe (Allocate / Set / Close), and must be
// write-locked.
//
// Methods which read from the FAT are thread-safe (Get) among themselves, and can be read-locked.
package fat

import (
	"errors"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/bitops"
	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/cluster/fat/fsinfo"
	"fuchsia.googlesource.com/thinfs/thinio"
)

var (
	// ErrHardIO indicates there was an unrecoverable error.
	ErrHardIO = errors.New("FAT: Hard Error I/O bit set. Run 'fsck <device> fat' on the partition containing this filesystem")

	// ErrDirtyFAT indicates the FAT was not unmounted properly.
	ErrDirtyFAT = errors.New("FAT: Dirty volume bit set. Run 'fsck <device> fat' on the partition containing this filesystem")

	// ErrNotOpen indicates the FAT cannot be accessed because it is not open.
	ErrNotOpen = errors.New("FAT: FAT not open")

	// ErrInvalidCluster indicates the provided cluster is invalid.
	ErrInvalidCluster = errors.New("FAT: Attempting to access FAT with invalid cluster")

	// ErrNoSpace indicates there is no remaining space in the FAT.
	ErrNoSpace = fs.ErrNoSpace
)

const (
	// Masks for usable cluster numbers.
	maskFAT12 = 0x00000FFF
	maskFAT16 = 0x0000FFFF
	maskFAT32 = 0x0FFFFFFF

	// An entry value of "entryEOF..." or higher indicates the end of a file.
	eofFAT12 = 0x00000FF8
	eofFAT16 = 0x0000FFF8
	eofFAT32 = 0x0FFFFFF8

	// "BAD CLUSTER" indicates that this cluster is prone to disk errors.
	badFAT12 = 0x00000FF7
	badFAT16 = 0x0000FFF7
	badFAT32 = 0x0FFFFFF7

	// The high bits of the entry at FAT[1] are reserved, and contain special values.
	dirtyBitFAT16 = 0x00008000 // 1: Volume is "clean". 0: Volume is "dirty", did not dismount properly.
	dirtyBitFAT32 = 0x08000000
	errorBitFAT16 = 0x00004000 // 1: No disk errors. 0: Disk I/O error encountered; sectors have gone bad.
	errorBitFAT32 = 0x04000000
)

// FAT holds the File Allocation Table.
type FAT struct {
	device *thinio.Conductor
	br     *bootrecord.Bootrecord

	readonly bool   // Are we preventing writes to the FAT?
	mirror   bool   // Are we mirroring to multiple FATs?
	primary  uint32 // Which FAT are we using?
	numFATs  uint32 // How many FATs are we mirroring to?

	closed bool

	// FAT32-exclusive FS Info values
	fsInfoValid bool
	freeCount   uint32 // TODO(smklein): Use this value
	nextFree    uint32
}

// Open opens a new File Allocation Table. This FAT will NOT be thread-safe; thread safety will
// need to be implemented by the layer above the FAT.
//
// If the filesystem is writeable, also marks the dirty bit as "dirty".
func Open(d *thinio.Conductor, br *bootrecord.Bootrecord, readonly bool) (*FAT, error) {
	glog.V(2).Info("Creating new FAT")
	mirror, numFATs, primary := br.MirroringInfo()
	f := &FAT{
		device:   d,
		br:       br,
		readonly: readonly,
		mirror:   mirror,
		primary:  primary,
		numFATs:  numFATs,
	}
	if f.isHardError() {
		return nil, ErrHardIO
	} else if f.isDirty() {
		return nil, ErrDirtyFAT
	}
	if !f.readonly {
		glog.V(2).Info("Setting FAT as dirty (writeable)")
		if err := f.setDirty(true); err != nil {
			return nil, err
		}
		// If the dirty bit is not flushed, it is possible for parts of the filesytem (other than
		// the dirty bit) to reach persistent storage first, which can make a dirty filesystem
		// incorrectly appear clean.
		f.device.Flush()
	}

	// Gather the FS Info values (if they exist).
	if fsInfoOffset, err := f.br.FsInfoOffset(); err == nil {
		if f.freeCount, f.nextFree, err = fsinfo.ReadHints(f.device, fsInfoOffset); err == nil {
			f.fsInfoValid = true
		}
	}
	if !f.fsInfoValid {
		f.freeCount = fsinfo.UnknownFlag
		f.nextFree = bootrecord.NumReservedClusters
	}

	return f, nil
}

// Close closes a File Allocation Table.
//
// If the filesystem was writeable, this marks the dirty bit as "clean".
func (f *FAT) Close() error {
	glog.V(2).Info("Closing FAT")
	if f.closed {
		return ErrNotOpen
	} else if !f.readonly {
		glog.V(2).Info("Setting FAT dirty status to clean")
		f.setDirty(false)
		if fsInfoOffset, err := f.br.FsInfoOffset(); err == nil && f.fsInfoValid {
			// Errors are ignored here intentionally -- if, for some reason, we are unable to write
			// "FreeCount" or "NextFree", we still want to flush the device and mark the FAT
			// structure as closed.
			fsinfo.WriteFreeCount(f.device, fsInfoOffset, f.freeCount)
			fsinfo.WriteNextFree(f.device, fsInfoOffset, f.nextFree)
		}
		f.device.Flush()
	}
	f.closed = true
	return nil
}

// EOFValue returns the FAT value that means "end of file".
// Notably, there are multiple values that mean EOF to FAT.
func (f *FAT) EOFValue() uint32 {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return eofFAT32
	case bootrecord.FAT16:
		return eofFAT16
	case bootrecord.FAT12:
		return eofFAT12
	default:
		panic("Unhandled FAT version")
	}
}

// FreeValue returns the FAT value that means "free cluster".
func (f *FAT) FreeValue() uint32 {
	return 0
}

// IsEOF describes if the entry signifies "End of File".
func (f *FAT) IsEOF(e uint32) bool {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return e >= eofFAT32
	case bootrecord.FAT16:
		return e >= eofFAT16
	case bootrecord.FAT12:
		return e >= eofFAT12
	default:
		panic("Unhandled FAT version")
	}
}

// IsFree describes if the entry signifies "free cluster".
func (f *FAT) IsFree(e uint32) bool {
	return e == 0
}

// SetHardError marks that the volume has encountered a disk I/O error.
func (f *FAT) SetHardError() error {
	// Error Bit exists in FAT[1].
	if f.closed {
		return ErrNotOpen
	} else if f.readonly {
		return fs.ErrReadOnly
	}

	offset := f.br.ClusterLocationFATPrimary(1)
	v, err := f.getRawEntry(offset)
	if err != nil {
		return err
	}
	// Clearing the bit marks the volume as erroneous.
	v &^= f.errorBit()
	return f.setRawEntry(v, offset)
}

// Get gets the value of a cluster entry.
// Can only be used to access clusters which are not reserved.
// Returns an error if accessing out-of-bounds cluster.
func (f *FAT) Get(cluster uint32) (uint32, error) {
	if f.closed {
		return 0, ErrNotOpen
	} else if !f.br.ClusterInValidRange(cluster) {
		return 0, ErrInvalidCluster
	}

	offset := int64(f.br.ClusterLocationFATPrimary(cluster))
	entry, err := f.getRawEntry(offset)
	if err != nil {
		return 0, err
	}
	return (entry & f.clusterMask()), nil
}

// Allocate returns the next known free cluster, starting from cluster "start".
// If a cluster is found, it is set to an EOF value (instead of free).
//
// If start is invalid, it is ignored. If no entries remain, an error is returned.
func (f *FAT) Allocate() (uint32, error) {
	if f.closed {
		return 0, ErrNotOpen
	} else if f.readonly {
		return 0, fs.ErrReadOnly
	}

	glog.V(2).Infof("Allocating from: %x\n", f.nextFree)
	isFreeAt := func(cluster uint32) bool {
		entry, err := f.Get(cluster)
		if err != nil {
			panic("Allocate not checking bounds properly")
		} else if f.IsFree(entry) && !f.isBad(cluster) {
			// The entry must be free, but we also should ensure we don't allocate the entry which
			// means "BAD CLUSTER".
			return true
		}
		return false
	}

	minCluster := bootrecord.NumReservedClusters
	maxCluster := minCluster + f.br.NumUsableClusters()
	if !f.br.ClusterInValidRange(f.nextFree) {
		// Ignore start if invalid (somehow).
		f.nextFree = minCluster
	}

	start := f.nextFree
	for ; f.nextFree < maxCluster; f.nextFree++ {
		if isFreeAt(f.nextFree) {
			return f.nextFree, f.Set(f.EOFValue(), f.nextFree)
		}
	}
	for f.nextFree = minCluster; f.nextFree < start; f.nextFree++ {
		if isFreeAt(f.nextFree) {
			return f.nextFree, f.Set(f.EOFValue(), f.nextFree)
		}
	}
	return 0, ErrNoSpace
}

// Set sets the value of a cluster entry.
//
// Returns an error if accessing an out-of-bounds cluster, or setting a cluster to point to itself.
func (f *FAT) Set(value uint32, cluster uint32) error {
	if f.closed {
		return ErrNotOpen
	} else if f.readonly {
		return fs.ErrReadOnly
	}

	glog.V(2).Infof("Setting cluster %x to value %x\n", cluster, value)
	if !f.br.ClusterInValidRange(cluster) {
		return ErrInvalidCluster
	} else if value == cluster {
		// FAT cluster cannot point to itself; this creates a loop.
		return ErrInvalidCluster
	}

	// Write to the primary first. Only return an error if the write to the primary fails.
	offset := int64(f.br.ClusterLocationFATPrimary(cluster))
	status, err := f.setValueAt(value, offset)
	if err != nil {
		return err
	}

	// Update the number of free clusters (if it has changed).
	if status == becameFree {
		f.freeCount++
	} else if status == becameAllocated {
		f.freeCount--
	}

	if f.mirror {
		// Mirroring mandates we write to multiple FATs at the same time.
		// Don't bother checking errors here -- what would we even do if a write to the primary
		// succeeded, but a write to the backup FAT failed?
		for indexFAT := uint32(0); indexFAT < f.numFATs; indexFAT++ {
			if indexFAT != f.primary {
				offset := int64(f.br.ClusterLocationFAT(indexFAT, cluster))
				f.setValueAt(value, offset)
			}
		}
	}
	return nil
}

func (f *FAT) clusterMask() uint32 {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return maskFAT32
	case bootrecord.FAT16:
		return maskFAT16
	default:
		panic("Unhandled FAT version")
	}
}

func (f *FAT) dirtyBit() uint32 {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return dirtyBitFAT32
	case bootrecord.FAT16:
		return dirtyBitFAT16
	default:
		panic("Unhandled FAT version")
	}
}

func (f *FAT) errorBit() uint32 {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return errorBitFAT32
	case bootrecord.FAT16:
		return errorBitFAT16
	default:
		panic("Unhandled FAT version")
	}
}

// getRawEntry accesses the full entry (appropriate for FAT type) and returns it as-is.
func (f *FAT) getRawEntry(offset int64) (uint32, error) {
	glog.V(2).Infof("Getting raw entry at %d", offset)
	buf := make([]byte, f.br.FATEntrySize())
	_, err := f.device.ReadAt(buf, offset)
	if err != nil {
		return 0, err
	}
	switch f.br.Type() {
	case bootrecord.FAT32:
		return bitops.GetLE32(buf), nil
	case bootrecord.FAT16:
		return uint32(bitops.GetLE16(buf)), nil
	default:
		panic("Unknown FAT type")
	}
}

// setRawEntry accesses the full entry (appropriate for FAT type) and sets it as-is.
func (f *FAT) setRawEntry(value uint32, offset int64) error {
	glog.V(2).Infof("Setting raw entry at %d", offset)
	buf := make([]byte, f.br.FATEntrySize())
	switch f.br.Type() {
	case bootrecord.FAT32:
		bitops.PutLE32(buf, uint32(value))
	case bootrecord.FAT16:
		bitops.PutLE16(buf, uint16(value))
	default:
		panic("Unknown FAT type")
	}
	_, err := f.device.WriteAt(buf, offset)
	return err
}

// Status identifying the change between free / allocated for a particular cluster. This helps keep
// track of the total number of free vs allocated clusters.
type freeStatus int

const (
	becameFree freeStatus = iota
	becameAllocated
	unchanged
)

// Internal "set", which sets a FAT entry at a known offset in the disk.
// Returns the freeStatus, providing information about the number of free vs allocated clusters.
func (f *FAT) setValueAt(value uint32, offset int64) (freeStatus, error) {
	v, err := f.getRawEntry(offset)
	if err != nil {
		return unchanged, err
	}
	freeBefore := f.IsFree(v & f.clusterMask())
	freeAfter := f.IsFree(value)
	if f.isBad(v & f.clusterMask()) {
		// This cluster value indicates "bad sector", and cannot be used.
		return unchanged, ErrInvalidCluster
	}
	// Preserve the top bits -- they are reserved.
	v &= ^f.clusterMask()
	v |= f.clusterMask() & value
	if err := f.setRawEntry(v, offset); err != nil {
		return unchanged, err
	}

	status := unchanged
	if freeBefore && !freeAfter {
		status = becameAllocated
	} else if !freeBefore && freeAfter {
		status = becameFree
	}
	return status, nil
}

// isBad describes if the entry points to an unallocatable sector.
func (f *FAT) isBad(e uint32) bool {
	switch f.br.Type() {
	case bootrecord.FAT32:
		return e == badFAT32
	case bootrecord.FAT16:
		return e == badFAT16
	case bootrecord.FAT12:
		return e == badFAT12
	default:
		panic("Unhandled FAT version")
	}
}

// setDirty marks that the volume has been mounted and may be in a modified state.
func (f *FAT) setDirty(m bool) error {
	if f.br.Type() == bootrecord.FAT12 {
		return nil
	} else if err := f.setDirtyLinux(m); err != nil {
		return err
	} else if err := f.setDirtyBSD(m); err != nil {
		return err
	}
	return nil
}

func (f *FAT) dirtyByteOffsetLinux() int64 {
	// NOTE: An undocumented 'dirty bit' exists in the bottom bit of either:
	// 1) Byte 0x41 (FAT32), or
	// 2) Byte 0x25 (FAT16) of the bootsector.
	if f.br.Type() == bootrecord.FAT32 {
		return 0x41
	}
	return 0x25
}

// Linux and BSD use distinct bits to represent that the FAT filesystem is dirty.  To be as
// conservative as possible, flip both bits to dirty when mounting a filesystem, and flip them both
// to clean when unmounting.
func (f *FAT) setDirtyLinux(m bool) error {
	offset := f.dirtyByteOffsetLinux()
	var buf [1]byte
	if _, err := f.device.ReadAt(buf[:], offset); err != nil {
		return err
	} else if m { // Setting Dirty
		buf[0] |= 0x01
	} else { // Setting Clean
		buf[0] &^= 0x01
	}
	_, err := f.device.WriteAt(buf[:], offset)
	return err
}

func (f *FAT) setDirtyBSD(m bool) error {
	offset := f.br.ClusterLocationFATPrimary(1)
	v, err := f.getRawEntry(offset)
	if err != nil {
		return err
	} else if m { // Clearing the bit marks the volume as dirty
		v &^= f.dirtyBit()
	} else { // Setting the bit marks the volume as clean
		v |= f.dirtyBit()
	}
	return f.setRawEntry(v, offset)
}

// isDirty returns true if the volume is dirty (i.e., it was not dismounted properly).
func (f *FAT) isDirty() bool {
	if f.br.Type() == bootrecord.FAT12 {
		return false
	}
	return f.isDirtyLinux() || f.isDirtyBSD()
}

func (f *FAT) isDirtyLinux() bool {
	offset := f.dirtyByteOffsetLinux()
	var buf [1]byte
	_, err := f.device.ReadAt(buf[:], offset)
	if err != nil {
		// If we can't identify the dirty bit, assume the volume is dirty.
		return true
	}
	return (buf[0] & 0x01) != 0
}

func (f *FAT) isDirtyBSD() bool {
	// Dirty Bit exists in FAT[1]
	offset := f.br.ClusterLocationFATPrimary(1)
	v, err := f.getRawEntry(offset)
	if err != nil {
		// If we can't identify the dirty bit, assume the volume is dirty.
		return true
	}
	return (v & f.dirtyBit()) == 0
}

// isHardError returns true if a disk I/O error occurred the last time the volume was mounted.
func (f *FAT) isHardError() bool {
	if f.br.Type() == bootrecord.FAT12 {
		return false
	}

	// Error Bit exists in FAT[1].
	offset := f.br.ClusterLocationFATPrimary(1)
	v, err := f.getRawEntry(offset)
	if err != nil {
		// If we can't identify the error bit, assume there is was an I/O error.
		return true
	}
	return (v & f.errorBit()) == 0
}
