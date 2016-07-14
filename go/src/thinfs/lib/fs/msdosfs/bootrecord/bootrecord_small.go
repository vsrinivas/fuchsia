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
