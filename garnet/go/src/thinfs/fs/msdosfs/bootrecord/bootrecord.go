// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package bootrecord describes the first sectors of a partition, which hold filesystem metadata.
package bootrecord

import (
	"errors"
	"unsafe"

	"github.com/golang/glog"

	"thinfs/thinio"
)

const (
	// NumReservedClusters describes how many cluster numbers are reserved (FAT[0] and FAT[1]).
	NumReservedClusters uint32 = 2

	// BootrecordSize is the size of a bootrecord
	BootrecordSize = 512
)

// Bootrecord describes the underlying boot record, independent of FAT type.
type Bootrecord struct {
	t      FATType
	device *thinio.Conductor

	// Cached values which have been pulled from the bootrecord on disk.
	totalSectors      uint32
	sectorSize        uint32 // In bytes
	clusterSize       uint32 // In bytes
	sectorsPerCluster uint32
	numUsableClusters uint32
	sectorsPerFAT     uint32
	reservedSectors   uint32
	numFATs           uint32
	firstDataSector   uint32
	mirroringActive   bool
	primaryFAT        uint32

	// FAT32 exclusive
	rootCluster  uint32
	fsInfoOffset int64

	// FAT12/16 exclusive
	numRootEntriesMax uint32
}

// Type returns the type of the underlying Bootrecord.
func (b *Bootrecord) Type() FATType {
	return b.t
}

// FATEntrySize returns the size (in bytes) of a single entry in the FAT.
func (b *Bootrecord) FATEntrySize() uint32 {
	switch b.t {
	case FAT32:
		return 4
	case FAT16:
		return 2
	default:
		panic("Not supported")
	}
}

// ClusterSize returns the size of a single cluster.
// The size of a single cluster should be a power of two multiple of the sector size.
func (b *Bootrecord) ClusterSize() uint32 {
	return b.clusterSize
}

// ClusterInValidRange checks that the cluster is in a valid range.
// It does not access the entry corresponding to the cluster.
func (b *Bootrecord) ClusterInValidRange(cluster uint32) bool {
	minCluster := NumReservedClusters
	maxCluster := minCluster + b.NumUsableClusters()
	return (minCluster <= cluster && cluster <= maxCluster)
}

// VolumeSize returns the size of all sectors allocated to the FAT filesystem.
func (b *Bootrecord) VolumeSize() int64 {
	return int64(b.totalSectors) * int64(b.sectorSize)
}

// FsInfoOffset returns the offset in the device of the FS Info structure,
// or an error if the value is invalid.
func (b *Bootrecord) FsInfoOffset() (int64, error) {
	if b.fsInfoOffset == 0 {
		return 0, errors.New("Missing/Invalid FS Info structure")
	}
	return b.fsInfoOffset, nil
}

// MirroringInfo describes if mirroring is necessary.
//
// If mirroring is active, returns "true", along with the number of FATs which need to be mirrored.
// If mirroring is disabled, returns "false", along with the primary FAT which should be used.
func (b *Bootrecord) MirroringInfo() (active bool, numFATs, primary uint32) {
	return b.mirroringActive, b.numFATs, b.primaryFAT
}

// ClusterLocationFATPrimary returns device offset for a particular cluster index in the primary
// File Allocation Table.
func (b *Bootrecord) ClusterLocationFATPrimary(cluster uint32) int64 {
	indexFAT := b.primaryFAT
	return b.ClusterLocationFAT(indexFAT, cluster)
}

// ClusterLocationFAT returns device offset for a particular cluster index in an arbitrary File
// Allocation Table.
func (b *Bootrecord) ClusterLocationFAT(indexFAT, cluster uint32) int64 {
	offsetOfFAT := (b.reservedSectors + indexFAT*b.sectorsPerFAT) * b.sectorSize
	offsetInsideFAT := cluster * b.FATEntrySize()
	return int64(offsetOfFAT + offsetInsideFAT)
}

// ClusterLocationData returns the device offset for a cluster's data.
func (b *Bootrecord) ClusterLocationData(cluster uint32) int64 {
	clusterSector := ((cluster - 2) * b.sectorsPerCluster) + b.firstDataSector
	return int64(clusterSector) * int64(b.sectorSize)
}

// NumUsableClusters returns the number of non-reserved sectors.
func (b *Bootrecord) NumUsableClusters() uint32 {
	return b.numUsableClusters
}

// RootCluster returns the cluster number of the root directory. Returns an error if the root
// directory is not found in a cluster (as is the case for FAT12 and FAT16).
func (b *Bootrecord) RootCluster() uint32 {
	switch b.t {
	case FAT32:
		return b.rootCluster
	case FAT16, FAT12:
		panic("Root cluster does not exist outside FAT32")
	default:
		panic("Unsupported FAT version")
	}
}

// RootReservedInfo provides information for the root directory on FAT12 and FAT16 filesystems.
func (b *Bootrecord) RootReservedInfo() (offsetStart int64, numRootEntriesMax int64) {
	switch b.t {
	case FAT32:
		panic("Root is not in the reserved region for FAT32")
	case FAT16, FAT12:
		offsetStart = int64(b.sectorSize * (b.reservedSectors + (b.numFATs * b.sectorsPerFAT)))
		numRootEntriesMax = int64(b.numRootEntriesMax)
		return
	default:
		panic("Unsupported FAT version")
	}
}

// New returns a new Bootrecord described by the first 512 bytes of a partition.
// Returns an error if the first 512 bytes of the partition do not match a known (or supported) FAT
// version.
func New(d *thinio.Conductor) (*Bootrecord, error) {
	glog.V(1).Info("Creating a New bootrecord")
	buf := make([]byte, BootrecordSize)
	_, err := d.ReadAt(buf, 0)
	if err != nil {
		return nil, err
	}

	// One of these is valid, the other is not. We need to discover which is which.
	small := *(*brSmall)(unsafe.Pointer(&buf[0]))
	large := *(*brLarge)(unsafe.Pointer(&buf[0]))

	// This is not the "official way" to determine the FAT type, but it lets us distinguish betwen
	// the small and large types of FATs. We'll guess the type now, and continue validating it
	// later.
	sizeClassLarge := large.bpb.guessFATType()

	var fatType FATType
	var bpb *bpbShared
	var firstDataSector uint32
	if sizeClassLarge {
		// Probably FAT32.
		if err := large.Validate(); err != nil {
			return nil, err
		}
		fatType = FAT32
		bpb = &large.bpb
		firstDataSector = large.FirstDataSector()
	} else {
		// Probably FAT12/16.
		if fatType, err = small.Validate(); err != nil {
			return nil, err
		}
		bpb = &small.bpb
		firstDataSector = small.FirstDataSector()
	}

	br := &Bootrecord{
		t:      fatType,
		device: d,
	}

	br.totalSectors = bpb.TotalSectors()
	br.sectorSize = bpb.BytesPerSec()
	br.sectorsPerCluster = bpb.SectorsPerCluster()
	br.clusterSize = br.sectorsPerCluster * br.sectorSize
	br.numUsableClusters = bpb.TotalClusters(firstDataSector) - NumReservedClusters
	br.firstDataSector = firstDataSector
	br.reservedSectors = bpb.NumSectorsReserved()
	br.numFATs = bpb.NumFATs()

	switch br.t {
	case FAT32:
		glog.V(1).Info("Loading a FAT32 bootrecord")
		br.sectorsPerFAT = large.bpbExtended.SectorsPerFAT32()
		br.mirroringActive, br.primaryFAT = large.bpbExtended.MirroringInfo()
		br.rootCluster = large.bpbExtended.RootCluster()
		fsInfoSector := large.bpbExtended.FsInfoSector()
		if 0 < fsInfoSector && fsInfoSector < br.reservedSectors {
			// Only use fsInfoSector if it is in the "reserved sector" region.
			br.fsInfoOffset = int64(large.bpbExtended.FsInfoSector() * br.sectorSize)
		}

		if !br.ClusterInValidRange(br.rootCluster) {
			return nil, errors.New("Invalid root cluster")
		}
	case FAT16:
		glog.V(1).Info("Loading a FAT16 bootrecord")
		br.sectorsPerFAT = small.bpb.SectorsPerFAT16()
		// Mirroring assumed to be active on FAT16.
		br.mirroringActive = true
		br.primaryFAT = 0
		br.numRootEntriesMax = small.bpb.NumRootEntries()
	default:
		// At the moment, FAT12 is unsupported.
		glog.V(1).Info("Cannot load unsupported FAT type")
		return nil, errors.New("Unsupported version of FAT")
	}

	if d.DeviceSize() < br.VolumeSize() {
		return nil, errors.New("Cannot load filesystem: expects more sectors than device provides")
	}
	return br, nil
}
