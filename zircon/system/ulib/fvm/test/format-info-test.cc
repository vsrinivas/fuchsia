// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fvm/fvm.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// 256 KB
constexpr size_t kFvmSlizeSize = 8 * kBlockSize;

constexpr size_t kInitialDiskSize = 256 * kBlockSize;

constexpr size_t kMaxDiskSize = 1024 * kBlockSize;

constexpr size_t kAllocTableSize = fvm::AllocTableLength(kMaxDiskSize, kFvmSlizeSize);

constexpr size_t kPartitionTableSize = fvm::kVPartTableLength;

size_t CalculateSliceStart(size_t part_size, size_t part_table_size, size_t allocation_table_size) {
  // Round Up to the next block.
  return 2 *
         fbl::round_up(fvm::kBlockSize + part_table_size + allocation_table_size, fvm::kBlockSize);
}

TEST(FvmInfoTest, FromSuperblockNoGaps) {
  FormatInfo format_info =
      FormatInfo::FromPreallocatedSize(kMaxDiskSize, kMaxDiskSize, kFvmSlizeSize);

  // When there is no gap allocated and metadata size should match.
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_size());
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_allocated_size());
  EXPECT_EQ(fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize), format_info.slice_count());
  EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

  EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
            format_info.GetSuperblockOffset(SuperblockType::kSecondary));
  EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
            format_info.GetSliceStart(1));
  EXPECT_EQ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - AllocationTable::kOffset) /
                    sizeof(SliceEntry) -
                1,
            format_info.GetMaxAllocatableSlices());
  EXPECT_EQ((fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize)),
            format_info.GetMaxAddressableSlices(kMaxDiskSize));
  EXPECT_EQ(
      format_info.GetSliceStart(1) +
          kFvmSlizeSize * ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - kAllocTableOffset) /
                               sizeof(SliceEntry) -
                           1),
      format_info.GetMaxPartitionSize());
}

TEST(FvmInfoTest, FromSuperblockWithGaps) {
  FormatInfo format_info =
      FormatInfo::FromPreallocatedSize(kInitialDiskSize, kPartitionTableSize, kFvmSlizeSize);

  EXPECT_EQ(fvm::MetadataSize(kInitialDiskSize, kFvmSlizeSize), format_info.metadata_size());
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_allocated_size());
  EXPECT_EQ(fvm::UsableSlicesCount(kInitialDiskSize, kFvmSlizeSize), format_info.slice_count());
  EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

  EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
            format_info.GetSuperblockOffset(SuperblockType::kSecondary));
  EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
            format_info.GetSliceStart(1));

  EXPECT_EQ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - AllocationTable::kOffset) /
                    sizeof(SliceEntry) -
                1,
            format_info.GetMaxAllocatableSlices());
  EXPECT_EQ((fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize)),
            format_info.GetMaxAddressableSlices(kMaxDiskSize));
  EXPECT_EQ(
      format_info.GetSliceStart(1) +
          kFvmSlizeSize * ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - kAllocTableOffset) /
                               sizeof(SliceEntry) -
                           1),
      format_info.GetMaxPartitionSize());
}

TEST(FvmInfoTest, FromDiskSize) {
  FormatInfo format_info = FormatInfo::FromDiskSize(kMaxDiskSize, kFvmSlizeSize);

  // When there is no gap allocated and metadata size should match.
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_size());
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_allocated_size());
  EXPECT_EQ(fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize), format_info.slice_count());
  EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

  EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
            format_info.GetSuperblockOffset(SuperblockType::kSecondary));
  EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
            format_info.GetSliceStart(1));

  EXPECT_EQ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - AllocationTable::kOffset) /
                    sizeof(SliceEntry) -
                1,
            format_info.GetMaxAllocatableSlices());
  EXPECT_EQ((fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize)),
            format_info.GetMaxAddressableSlices(kMaxDiskSize));
  EXPECT_EQ(
      format_info.GetSliceStart(1) +
          kFvmSlizeSize * ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - kAllocTableOffset) /
                               sizeof(SliceEntry) -
                           1),
      format_info.GetMaxPartitionSize());
}

TEST(FvmInfoTest, FromPreallocatedSizeWithGaps) {
  FormatInfo format_info =
      FormatInfo::FromPreallocatedSize(kInitialDiskSize, kMaxDiskSize, kFvmSlizeSize);

  EXPECT_EQ(fvm::MetadataSize(kInitialDiskSize, kFvmSlizeSize), format_info.metadata_size());
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_allocated_size());
  EXPECT_EQ(fvm::UsableSlicesCount(kInitialDiskSize, kFvmSlizeSize), format_info.slice_count());
  EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

  EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
  EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
            format_info.GetSuperblockOffset(SuperblockType::kSecondary));
  EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
            format_info.GetSliceStart(1));

  EXPECT_EQ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - AllocationTable::kOffset) /
                    sizeof(SliceEntry) -
                1,
            format_info.GetMaxAllocatableSlices());
  EXPECT_EQ((fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize)),
            format_info.GetMaxAddressableSlices(kMaxDiskSize));
  EXPECT_EQ(
      format_info.GetSliceStart(1) +
          kFvmSlizeSize * ((fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize) - kAllocTableOffset) /
                               sizeof(SliceEntry) -
                           1),
      format_info.GetMaxPartitionSize());
}

TEST(FvmInfoTest, FromPreallocatedSizeNthEntryOOB) {
  // This test triggers the edge case when the metadata can address the nth slice.
  FormatInfo format_info =
      FormatInfo::FromPreallocatedSize(kInitialDiskSize, kMaxDiskSize, kFvmSlizeSize);

  // The number of usable slices in the Disk is big enough that we limit the number of slices to
  // those that fit in the metadata. This will match the metadata size, the last
  // allocateable slice will be OOB, which is why the MaxAddressable slice will be before it.
  EXPECT_EQ(1023, format_info.GetMaxAllocatableSlices());
  EXPECT_EQ(format_info.GetMaxAllocatableSlices(),
            format_info.GetMaxAddressableSlices(kMaxDiskSize * 10));
}

}  // namespace
}  // namespace fvm
