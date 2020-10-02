// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fvm/fvm.h>
#include <zxtest/zxtest.h>

namespace fvm {

TEST(FvmFormat, DefaultInitializedGetterValues) {
  Header header;

  // Currently the partition table has a constant size so we always return it. Arguably we could
  // also return 0 for these.
  EXPECT_EQ(kBlockSize, header.GetPartitionTableOffset());
  EXPECT_EQ(kMaxVPartitions - 1, header.GetPartitionTableEntryCount());
  EXPECT_EQ(65536, header.GetPartitionTableByteSize());

  // The allocation table starts after the partition table and is empty. If we change the partition
  // table getters to return 0 in this case, the allocation table offset could also be changed.
  EXPECT_EQ(header.GetPartitionTableOffset() + header.GetPartitionTableByteSize(),
            header.GetAllocationTableOffset());
  EXPECT_EQ(0u, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(0u, header.GetAllocationTableUsedByteSize());
  EXPECT_EQ(0u, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(0u, header.GetAllocationTableAllocatedByteSize());

  EXPECT_EQ(0u, header.GetMetadataUsedBytes());
  EXPECT_EQ(0u, header.GetMetadataAllocatedBytes());
}

TEST(FvmFormat, SliceConstructors) {
  constexpr size_t kInitialSliceCount = 2;
  constexpr size_t kMaxSliceCount = 4096;
  constexpr size_t kSmallSliceSize = kBlockSize;
  Header header = Header::FromGrowableSliceCount(kMaxUsablePartitions, kInitialSliceCount,
                                                 kMaxSliceCount, kSmallSliceSize);
  EXPECT_EQ(kInitialSliceCount, header.GetAllocationTableUsedEntryCount());
  // The constructor guarantees only that the table is "big enough" to handle the required slices,
  // but it could be larger depending on padding.
  EXPECT_LT(kMaxSliceCount, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kSmallSliceSize, header.slice_size);
  EXPECT_EQ(header.GetSliceDataOffset(1) + kSmallSliceSize * kInitialSliceCount,
            header.fvm_partition_size);
}

TEST(FvmFormat, SizeConstructors) {
  // A growable partition that starts off with no slices.
  constexpr size_t kInitialDiskSize = 1;  // Too small for anything.
  constexpr size_t kMaxDiskSize = static_cast<size_t>(1024) * 1024 * 1024 * 1024;  // 1TB
  constexpr size_t kBigSliceSize = 1024 * 1024;
  Header header = Header::FromGrowableDiskSize(kMaxUsablePartitions, kInitialDiskSize, kMaxDiskSize,
                                               kBigSliceSize);
  // No allocated slices since it's too small.
  EXPECT_EQ(0, header.GetAllocationTableUsedEntryCount());
  EXPECT_LT(kMaxDiskSize / kBigSliceSize, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kBigSliceSize, header.slice_size);

  size_t metadata_size = header.GetSliceDataOffset(1);

  // Test an input disk size that's one too small for two slices. The slice count should always
  // be rounded down so there are only full slices, so we should be left with one current slicw.
  size_t round_down_disk_size = metadata_size + kBigSliceSize * 2 - 1;
  header = Header::FromGrowableDiskSize(kMaxUsablePartitions, round_down_disk_size, kMaxDiskSize,
                                        kBigSliceSize);
  EXPECT_EQ(1, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(metadata_size + kBigSliceSize, header.fvm_partition_size);

  // A large non-growable disk. This one has block size == slice size so all of the disk should
  // be addressable with no rounding.
  constexpr size_t kSmallSliceSize = kBlockSize;
  header = Header::FromDiskSize(kMaxUsablePartitions, kMaxDiskSize, kSmallSliceSize);
  EXPECT_LT(kMaxDiskSize / kSmallSliceSize, header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(kMaxDiskSize, header.fvm_partition_size);
}

TEST(FvmFormat, Getters) {
  constexpr size_t kUsedSlices = 5;
  Header header{
      .magic = kMagic,
      .version = kVersion,
      .pslice_count = kUsedSlices,
      .slice_size = kBlockSize * 2,
      .fvm_partition_size = kBlockSize,
      // TODO(fxb/40192): Try different values here.
      .vpartition_table_size = kMaxVPartitions * sizeof(VPartitionEntry),
      .allocation_table_size = kBlockSize * 2,
      .generation = 0,
  };

  // The partition table starts at the block following the superblock.
  EXPECT_EQ(kBlockSize, header.GetPartitionTableOffset());

  // The number of usable entries in the partition table is one less that the number of slots.
  // TODO(fxb/59980) make this consistent so we can use the whole table. Either use 0-1023 as the
  // valid partition range, or 1-1024.
  EXPECT_EQ(header.vpartition_table_size / sizeof(VPartitionEntry) - 1,
            header.GetPartitionTableEntryCount());

  // The byte size is trivial. Currently this is fixed.
  // TODO(fxb/40192): Use this value so the partition table can have different sizes:
  //   EXPECT_EQ(header.vpartition_table_size, header.GetPartitionTableByteSize());
  EXPECT_EQ(65536, header.GetPartitionTableByteSize());

  // The allocation table follows the partition table. The allocated byte size just comes from the
  // header directly.
  EXPECT_EQ(kBlockSize + 65536, header.GetAllocationTableOffset());
  EXPECT_EQ(header.allocation_table_size, header.GetAllocationTableAllocatedByteSize());

  // The number of usable entries in the table is one less than the number that will fix.
  // TODO(fxb/59980) use all the slots:
  //   EXPECT_EQ(header.allocation_table_size / sizeof(SliceEntry),
  //             header.GetAllocationTableAllocatedEntryCount());
  EXPECT_EQ(header.allocation_table_size / sizeof(SliceEntry) - 1,
            header.GetAllocationTableAllocatedEntryCount());

  // The number of used slices. The bytes covered is rounded up the next block size.
  EXPECT_EQ(kUsedSlices, header.GetAllocationTableUsedEntryCount());
  EXPECT_EQ(kBlockSize, header.GetAllocationTableUsedByteSize());

  // The full metadata covers to the end of the allocation table.
  EXPECT_EQ(header.GetAllocationTableOffset() + header.GetAllocationTableUsedByteSize(),
            header.GetMetadataUsedBytes());
  EXPECT_EQ(header.GetAllocationTableOffset() + header.GetAllocationTableAllocatedByteSize(),
            header.GetMetadataAllocatedBytes());
}

}  // namespace fvm
