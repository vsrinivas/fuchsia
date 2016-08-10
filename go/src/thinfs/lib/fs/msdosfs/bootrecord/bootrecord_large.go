// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootrecord

import (
	"errors"
	"fmt"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/lib/bitops"
)

const (
	extFlagMaskFAT           = 0x0F
	extFlagMirroringDisabled = 0x80
)

// This file describes the on-disk format for large bootrecords.
// The bootrecord exists in the first sector of the partition containing the FAT filesystem.
// NOTE: All following structures in this file are stored on disk, which is why they only use the
// uint8 type.

// brLarge is the prefix of the first sector of the FAT32 filesystem.
type brLarge struct {
	bsPrefix      bootsectorPrefix
	bpb           bpbShared
	bpbExtended   bpbLarge
	bsSuffix      bootsectorSuffix
	padding       [420]uint8 // Padding so struct is 512 bytes
	bootSectorSig [2]uint8
}

func (b *brLarge) Validate() error {
	// The suffix should be validated, but only AFTER determining FAT type.
	sizeClassLarge := true
	if err := bootSectorSignatureValid(b.bootSectorSig[:]); err != nil {
		return err
	} else if err := b.bsPrefix.validate(); err != nil {
		return err
	} else if err := b.bpb.validate(sizeClassLarge); err != nil {
		return err
	} else if err := b.bsSuffix.validate(); err != nil {
		// FAT32-exclusive
		return err
	} else if err := b.validateType(); err != nil {
		// FAT32-exclusive
		return err
	}

	version := b.bpbExtended.FsVersion()
	if version != 0 {
		glog.Warning("Loading a FAT32 filesystem with an unexpected version: %d", version)
	}

	if b.bpbExtended.SectorsPerFAT32() == 0 {
		return errors.New("FAT32 must set sectorsPerFAT32 to a nonzero value")
	}

	reservedSectors := b.bpb.NumSectorsReserved()
	backupSector := b.bpbExtended.BackupSector()
	fsInfoSector := b.bpbExtended.FsInfoSector()
	if backupSector == 0 || backupSector == fsInfoSector || reservedSectors <= backupSector {
		return errors.New("Invalid backup boot sector")
	} else if fsInfoSector == 0 || reservedSectors <= fsInfoSector {
		return errors.New("Invalid fsInfo sector")
	}

	mirroring, primaryIndex := b.bpbExtended.MirroringInfo()
	if !mirroring && primaryIndex >= b.bpb.NumFATs() {
		return errors.New("Mirroring disabled, but selected primary FAT is invalid")
	}

	return nil
}

// We made a guess that this bootrecord was FAT32. Double-check that assumption now.
func (b *brLarge) validateType() error {
	if b.bpb.NumRootEntries() != 0 {
		// Count the number of root directory sectors reserved for FAT32. Should be zero.
		return errors.New("FAT32 should not reserve space for root directories outside cluster space")
	}

	numUsableClusters := b.bpb.TotalClusters(b.FirstDataSector())
	if numUsableClusters < 65525 {
		return fmt.Errorf("Expected at least 65525 clusters for FAT32, but only saw: %d", numUsableClusters)
	}

	return nil
}

func (b *brLarge) FirstDataSector() uint32 {
	return b.bpb.NumSectorsReserved() + (b.bpb.NumFATs() * b.bpbExtended.SectorsPerFAT32())
}

// FAT32-exclusive fields.
type bpbLarge struct {
	sectorsPerFAT32 [4]uint8  // Sectors for ONE FAT
	extFlags        [2]uint8  // Extended flags
	fsVersion       [2]uint8  // High byte: Major revision, Low byte: Minor revision
	rootCluster     [4]uint8  // Cluster number of start of root directory
	fsInfoSector    [2]uint8  // Sector of FS Info structure
	backupSector    [2]uint8  // Sector number of backup copy of boot record
	reserved2       [12]uint8 // Reserved for future expansion
}

func (b *bpbLarge) SectorsPerFAT32() uint32 {
	return uint32(bitops.GetLE32(b.sectorsPerFAT32[:]))
}
func (b *bpbLarge) ExtFlags() uint32 {
	return uint32(bitops.GetLE16(b.extFlags[:]))
}
func (b *bpbLarge) FsVersion() uint32 {
	return uint32(bitops.GetLE16(b.fsVersion[:]))
}
func (b *bpbLarge) RootCluster() uint32 {
	return uint32(bitops.GetLE32(b.rootCluster[:]))
}
func (b *bpbLarge) FsInfoSector() uint32 {
	return uint32(bitops.GetLE16(b.fsInfoSector[:]))
}
func (b *bpbLarge) BackupSector() uint32 {
	return uint32(bitops.GetLE16(b.backupSector[:]))
}

func (b *bpbLarge) MirroringInfo() (active bool, primaryIndex uint32) {
	ext := b.ExtFlags()
	if ext&extFlagMirroringDisabled == 0 {
		// Mirroring to all FATs. We are returning "0" as the primary, but any FAT could be used.
		return true, 0
	}
	// Only a single FAT is active.
	return false, ext & extFlagMaskFAT
}
