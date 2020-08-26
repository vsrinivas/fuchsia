// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <limits>
#include <memory>
#include <utility>

#include <fvm/fvm.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// 1 MB slice.
constexpr uint64_t kSliceSize = 1lu << 20;

// 4 GB fvm_partition_size.
constexpr uint64_t kPartitionSize = 4lu << 30;

struct Metadata {
  std::unique_ptr<uint8_t[]> superblock;
  size_t size;
  size_t capacity;
};

Metadata CreateSuperblock(uint64_t initial_disk_size, uint64_t maximum_disk_capacity) {
  FormatInfo info =
      FormatInfo::FromPreallocatedSize(initial_disk_size, maximum_disk_capacity, kSliceSize);

  Metadata metadata;
  metadata.superblock = std::make_unique<uint8_t[]>(info.metadata_allocated_size());
  metadata.size = info.metadata_size();
  metadata.capacity = info.metadata_allocated_size();
  memset(metadata.superblock.get(), 0, metadata.capacity);
  memcpy(metadata.superblock.get(), &info.header(), sizeof(Header));
  return metadata;
}

TEST(IntegrityValidationTest, BothHashesAreOkPickLatest) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_2.superblock.get())->generation =
      reinterpret_cast<fvm::Header*>(metadata_1.superblock.get())->generation + 1;
  UpdateHash(metadata_1.superblock.get(), metadata_1.size);
  UpdateHash(metadata_2.superblock.get(), metadata_2.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_2.superblock.get());
}

TEST(IntegrityValidationTest, PrimaryIsOkAndSecondaryIsCorruptedPicksPrimary) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_2.superblock.get())->fvm_partition_size = 0;
  UpdateHash(metadata_1.superblock.get(), metadata_1.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_1.superblock.get());
}

TEST(IntegrityValidationTest, PrimaryIsCorruptedAndSecondaryIsOkPicksSecondary) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_1.superblock.get())->fvm_partition_size = 0;
  UpdateHash(metadata_2.superblock.get(), metadata_2.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_2.superblock.get());
}

TEST(IntegrityValidationTest, BothAreCorruptedIsBadState) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_1.superblock.get())->fvm_partition_size = 0;
  reinterpret_cast<fvm::Header*>(metadata_2.superblock.get())->fvm_partition_size = 0;

  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_EQ(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, nullptr),
            ZX_ERR_BAD_STATE);
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnPrimaryPicksSecondary) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_1.superblock.get())->allocation_table_size = 0;
  UpdateHash(metadata_2.superblock.get(), metadata_2.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_2.superblock.get());
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnSecondaryPicksPrimary) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_2.superblock.get())->allocation_table_size = 0;
  UpdateHash(metadata_1.superblock.get(), metadata_1.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_1.superblock.get());
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnBothIsBadState) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  reinterpret_cast<fvm::Header*>(metadata_1.superblock.get())->allocation_table_size = 0;
  reinterpret_cast<fvm::Header*>(metadata_2.superblock.get())->allocation_table_size = 0;

  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_EQ(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, nullptr),
            ZX_ERR_BAD_STATE);
}

TEST(IntegrityValidationTest, ValidatesMetadataSizeNotCapacity) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  UpdateHash(metadata_1.superblock.get(), metadata_1.size);
  // This is not taken into account when validating the metadata header, we only check the data
  // we are actually using.
  memset(metadata_1.superblock.get() + metadata_1.size, 1, metadata_1.capacity - metadata_1.size);

  const void* picked_metadata;
  ASSERT_EQ(metadata_1.size, metadata_2.size);
  ASSERT_EQ(metadata_1.capacity, metadata_2.capacity);
  ASSERT_OK(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, &picked_metadata));
  EXPECT_EQ(picked_metadata, metadata_1.superblock.get());
}

TEST(IntegrityValidationTest, ZeroedHeaderIsBadState) {
  Metadata metadata_1 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  memset(metadata_1.superblock.get(), 0, metadata_1.capacity);

  Metadata metadata_2 = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  memset(metadata_2.superblock.get(), 0, metadata_2.capacity);

  ASSERT_EQ(ValidateHeader(metadata_1.superblock.get(), metadata_2.superblock.get(),
                           metadata_1.capacity, nullptr),
            ZX_ERR_BAD_STATE);
}

TEST(IntegrityValidationTest, MetadataHasOverflowInCalculatedSizeIsBadState) {
  Metadata metadata = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  auto* header = reinterpret_cast<fvm::Header*>(metadata.superblock.get());

  header->allocation_table_size = std::numeric_limits<uint64_t>::max() - fvm::kAllocTableOffset + 1;

  ASSERT_EQ(ValidateHeader(metadata.superblock.get(), metadata.superblock.get(), metadata.capacity,
                           nullptr),
            ZX_ERR_BAD_STATE);
}

TEST(IntegrityValidationTest, FvmPartitionNotBigForBothCopiesOfMetadataIsBadState) {
  Metadata metadata = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Header* header = reinterpret_cast<Header*>(metadata.superblock.get());
  FormatInfo info(*header);

  header->fvm_partition_size = 2 * info.metadata_allocated_size() - 1;

  ASSERT_EQ(ValidateHeader(metadata.superblock.get(), metadata.superblock.get(), metadata.capacity,
                           nullptr),
            ZX_ERR_BAD_STATE);
}

TEST(IntegrityValidationTest, LastSliceOutOfFvmPartitionIsBadState) {
  Metadata metadata = CreateSuperblock(kPartitionSize, 2 * kPartitionSize);
  Header* header = reinterpret_cast<Header*>(metadata.superblock.get());
  FormatInfo info(*header);

  // Now the last slice ends past the fvm partition and would trigger a Page Fault, probably.
  header->fvm_partition_size = info.GetSliceStart(0) + info.slice_count() * info.slice_size() - 1;

  ASSERT_EQ(ValidateHeader(metadata.superblock.get(), metadata.superblock.get(), metadata.capacity,
                           nullptr),
            ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace fvm
