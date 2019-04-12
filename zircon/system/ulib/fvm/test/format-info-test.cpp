// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fvm/format.h>

#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// 8 KB
constexpr size_t kFvmBlockSize = 1 << 13;

// 256 KB
constexpr size_t kFvmSlizeSize = 1 << 18;

constexpr size_t kInitialDiskSize = 256 * kFvmBlockSize;

constexpr size_t kMaxDiskSize = 1024 * kFvmBlockSize;

constexpr size_t kAllocTableSize = fvm::AllocTableLength(kMaxDiskSize, kFvmSlizeSize);

constexpr size_t kPartitionTableSize = fvm::kVPartTableLength;

fvm_t MakeSuperBlock(size_t part_size, size_t part_table_size, size_t alloc_table_size) {
    fvm_t superblock;
    superblock.fvm_partition_size = part_size;
    superblock.vpartition_table_size = part_table_size;
    superblock.allocation_table_size = alloc_table_size;
    superblock.slice_size = kFvmSlizeSize;
    superblock.version = fvm::kMagic;
    superblock.magic = fvm::kVersion;
    superblock.generation = 1;
    fvm_update_hash(&superblock, sizeof(fvm_t));
    return superblock;
}

size_t CalculateSliceStart(size_t part_size, size_t part_table_size, size_t allocation_table_size) {
    // Round Up to the next block.
    return 2 * fbl::round_up(fvm::kBlockSize + part_table_size + allocation_table_size,
                             fvm::kBlockSize);
}

TEST(FvmInfoTest, FromSuperblockNoGaps) {
    fvm_t superblock = MakeSuperBlock(kMaxDiskSize, kPartitionTableSize, kAllocTableSize);
    FormatInfo format_info = FormatInfo::FromSuperBlock(superblock);

    // When there is no gap allocated and metadata size should match.
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_size());
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.metadata_allocated_size());
    EXPECT_EQ(fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize), format_info.slice_count());
    EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

    EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.GetSuperblockOffset(SuperblockType::kSecondary));
    EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
              format_info.GetSliceStart(1));
}

TEST(FvmInfoTest, FromSuperblockWithGaps) {
    fvm_t superblock = MakeSuperBlock(kInitialDiskSize, kPartitionTableSize, kAllocTableSize);
    FormatInfo format_info = FormatInfo::FromSuperBlock(superblock);

    EXPECT_EQ(fvm::MetadataSize(kInitialDiskSize, kFvmSlizeSize), format_info.metadata_size());
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.metadata_allocated_size());
    EXPECT_EQ(fvm::UsableSlicesCount(kInitialDiskSize, kFvmSlizeSize), format_info.slice_count());
    EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

    EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.GetSuperblockOffset(SuperblockType::kSecondary));
    EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
              format_info.GetSliceStart(1));
}

TEST(FvmInfoTest, FromDiskSize) {
    FormatInfo format_info = FormatInfo::FromDiskSize(kMaxDiskSize, kFvmSlizeSize);

    // When there is no gap allocated and metadata size should match.
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize), format_info.metadata_size());
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.metadata_allocated_size());
    EXPECT_EQ(fvm::UsableSlicesCount(kMaxDiskSize, kFvmSlizeSize), format_info.slice_count());
    EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

    EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.GetSuperblockOffset(SuperblockType::kSecondary));
    EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
              format_info.GetSliceStart(1));
}

TEST(FvmInfoTest, FromPreallocatedSizeWithGaps) {
    FormatInfo format_info =
        FormatInfo::FromPreallocatedSize(kInitialDiskSize, kMaxDiskSize, kFvmSlizeSize);

    EXPECT_EQ(fvm::MetadataSize(kInitialDiskSize, kFvmSlizeSize), format_info.metadata_size());
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.metadata_allocated_size());
    EXPECT_EQ(fvm::UsableSlicesCount(kInitialDiskSize, kFvmSlizeSize), format_info.slice_count());
    EXPECT_EQ(kFvmSlizeSize, format_info.slice_size());

    EXPECT_EQ(0, format_info.GetSuperblockOffset(SuperblockType::kPrimary));
    EXPECT_EQ(fvm::MetadataSize(kMaxDiskSize, kFvmSlizeSize),
              format_info.GetSuperblockOffset(SuperblockType::kSecondary));
    EXPECT_EQ(CalculateSliceStart(kMaxDiskSize, kPartitionTableSize, kAllocTableSize),
              format_info.GetSliceStart(1));
}

} // namespace
} // namespace fvm
