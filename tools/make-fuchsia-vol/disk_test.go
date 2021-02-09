// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/thinfs/gpt"
)

const KILOBYTE = 1024
const MEGABYTE = 1024 * KILOBYTE

type AddressAndRange struct {
	addr          uint64
	expected_size uint64
}

func checkPartitionLayout(t *testing.T, sizes partitionSizes, layout partitionLayout, disk diskInfo) {
	// A zero FVM size means the partition should be filled.
	if sizes.fvmSize == 0 {
		sizes.fvmSize = disk.diskSize - (gpt.MinPartitionEntryArraySize + gpt.HeaderSize)
	}
	// The order should be: EFI, A, VBMETA_A, B, VBMETA_B, R, VBMETA_R, MISC, FVM.
	startAddresses := []AddressAndRange{
		{layout.efiStart, sizes.efiSize},
	}

	// A ZIRCON-A start address of zero should mean there's no ABR partitions on the disk.
	if layout.aStart != 0 {
		startAddresses = append(startAddresses,
			AddressAndRange{layout.aStart, sizes.abrSize},
			AddressAndRange{layout.vbmetaAStart, sizes.vbmetaSize},
			AddressAndRange{layout.bStart, sizes.abrSize},
			AddressAndRange{layout.vbmetaBStart, sizes.vbmetaSize},
			AddressAndRange{layout.rStart, sizes.abrSize},
			AddressAndRange{layout.vbmetaRStart, sizes.vbmetaSize},
			AddressAndRange{layout.miscStart, sizes.vbmetaSize},
		)
	}
	// FVM start address of zero means there's no FVM on the disk.
	if layout.fvmStart != 0 {
		startAddresses = append(startAddresses,
			AddressAndRange{layout.fvmStart, sizes.fvmSize},
		)
	}

	var prevPartition *gpt.PartitionEntry
	prevPartition = nil
	for i, partition := range disk.gpt.Primary.Partitions {
		expected := startAddresses[i]
		if partition.StartingLBA != expected.addr {
			t.Fatalf("Invalid partition start address: partition %s, got %d expected %d", partition.PartitionName, partition.StartingLBA, expected.addr)
		}

		// Check ordering of partitions and that they don't overlap.
		if prevPartition != nil {
			// Make sure all partitions have (start > end). A 1-block partition would have (start == end).
			if partition.StartingLBA > partition.EndingLBA {
				t.Fatalf("Partition %s starts after it ends.", partition.PartitionName)
			}
			// Make sure for each pair of consecutive partitions, (p1.end < p2.start)
			if partition.StartingLBA < prevPartition.EndingLBA {
				t.Fatalf("Partition %s starts before previous partition %s ends.", partition.PartitionName, prevPartition.PartitionName)
			}
		}

		// EndingLBA is inclusive, so we need to subtract one when using size to calculate the expected value.
		expectedSizeLBA := expected.expected_size / disk.logical
		expectedEnd := expected.addr - 1 + expectedSizeLBA
		if partition.EndingLBA != expectedEnd {
			t.Fatalf("Invalid partition end address: partition %s, got %d expected %d", partition.PartitionName, partition.EndingLBA, expectedEnd)
		}

		// Check partitions fall within the usable LBA range specified by the GPT.
		if partition.EndingLBA > disk.gpt.Primary.LastUsableLBA {
			t.Fatalf("Partition %s extends beyond the end of the disk!", partition.PartitionName)
		}
		if partition.StartingLBA < disk.gpt.Primary.FirstUsableLBA {
			t.Fatalf("Partition %s starts before the start of the disk!", partition.PartitionName)
		}

		prevPartition = &disk.gpt.Primary.Partitions[i]
	}
}

func TestDiskLayoutAllExplicit(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    2 * MEGABYTE,
	}

	layout := createPartitionTable(&info, &sizes, true, false, false, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutFvmFill(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    0,
	}

	layout := createPartitionTable(&info, &sizes, true, false, false, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutSmallerPartitions(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	// Most of these partitions are smaller than the physical block size.
	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    1 * KILOBYTE,
		vbmetaSize: 2 * KILOBYTE,
		fvmSize:    2 * KILOBYTE,
	}

	layout := createPartitionTable(&info, &sizes, true, false, false, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutNoAbr(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    0,
	}

	layout := createPartitionTable(&info, &sizes, false, false, false, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutNoFvm(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    0,
	}

	layout := createPartitionTable(&info, &sizes, true, false, true, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutNoAbrAndNoFvm(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    0,
	}

	layout := createPartitionTable(&info, &sizes, false, false, true, false)

	checkPartitionLayout(t, sizes, layout, info)
}

func TestDiskLayoutSparseFvm(t *testing.T) {
	info := diskInfo{
		physical: 4096,
		logical:  512,
		optimal:  0,
		diskSize: 512 * MEGABYTE,
		gpt:      gpt.GPT{},
	}

	sizes := partitionSizes{
		efiSize:    2 * MEGABYTE,
		abrSize:    2 * MEGABYTE,
		vbmetaSize: 2 * MEGABYTE,
		fvmSize:    34 * MEGABYTE,
	}

	layout := createPartitionTable(&info, &sizes, true, false, false, true)

	checkPartitionLayout(t, sizes, layout, info)
}
