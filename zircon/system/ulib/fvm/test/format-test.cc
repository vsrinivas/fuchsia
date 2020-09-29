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
