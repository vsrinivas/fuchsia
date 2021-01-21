// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootrecord

import "errors"

// brSmall is the prefix of the first sector of the FAT12/16 filesystem.
type brSmall struct {
	bsPrefix      bootsectorPrefix
	bpb           bpbShared
	bsSuffix      bootsectorSuffix
	padding       [448]uint8 // Padding so struct is 512 bytes long.
	bootSectorSig [2]uint8
}

func (b *brSmall) Validate() (FATType, error) {
	sizeClassLarge := false
	if err := bootSectorSignatureValid(b.bootSectorSig[:]); err != nil {
		return FATInvalid, err
	} else if err := b.bsPrefix.validate(); err != nil {
		return FATInvalid, err
	} else if err := b.bpb.validate(sizeClassLarge); err != nil {
		return FATInvalid, err
	} else if err := b.bsSuffix.validate(); err != nil {
		return FATInvalid, err
	}

	numUsableClusters := b.bpb.TotalClusters(b.FirstDataSector())
	if numUsableClusters == 0 {
		return FATInvalid, errors.New("No usable clusters")
	} else if numUsableClusters < 4085 {
		return FAT12, nil
	} else if numUsableClusters < 65525 {
		return FAT16, nil
	}

	return FATInvalid, errors.New("Too many clusters for FAT12/16")
}

func (b *brSmall) NumRootDirSectors() uint32 {
	numRootEntries := b.bpb.NumRootEntries()
	bytesPerSector := b.bpb.BytesPerSec()
	return ((numRootEntries * 32) + (bytesPerSector - 1)) / bytesPerSector
}

func (b *brSmall) FirstDataSector() uint32 {
	return b.bpb.NumSectorsReserved() + (b.bpb.NumFATs() * b.bpb.SectorsPerFAT16()) + b.NumRootDirSectors()
}
