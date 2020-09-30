// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <fvm/fvm.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// 1 MB slice.
constexpr uint64_t kSliceSize = 1lu << 20;

// 4 GB fvm_partition_size.
constexpr uint64_t kPartitionSize = 4lu << 30;

struct Metadata {
  std::vector<uint8_t> primary_buffer;
  std::vector<uint8_t> secondary_buffer;

  // Pointers to the beginnings the buffers for easier access.
  Header* primary = nullptr;
  Header* secondary = nullptr;

  void UpdateHash(SuperblockType type) {
    if (type == fvm::SuperblockType::kPrimary)
      ::fvm::UpdateHash(primary_buffer.data(), primary->GetMetadataUsedBytes());
    else
      ::fvm::UpdateHash(secondary_buffer.data(), secondary->GetMetadataUsedBytes());
  }

  std::optional<fvm::SuperblockType> ValidateHeader() const {
    return ::fvm::ValidateHeader(primary_buffer.data(), secondary_buffer.data(),
                                 primary_buffer.size());
  }
};

// Creates the metadata for the beginning of the device including both primary and secondary
// copies of the metadata. Both copies will be identical.
//
// The hashes will NOT be filled in (tests will generally set some values before doing the hash).
// Call Metadata::UpdateHash to fill these in.
Metadata CreateMetadata(uint64_t initial_disk_size, uint64_t maximum_disk_capacity) {
  Header header = Header::FromGrowableDiskSize(kMaxUsablePartitions, initial_disk_size,
                                               maximum_disk_capacity, kSliceSize);

  Metadata result;

  result.primary_buffer.resize(header.GetMetadataAllocatedBytes());
  memcpy(result.primary_buffer.data(), &header, sizeof(Header));
  result.secondary_buffer = result.primary_buffer;

  result.primary = reinterpret_cast<Header*>(result.primary_buffer.data());
  result.secondary = reinterpret_cast<Header*>(result.secondary_buffer.data());

  return result;
}

TEST(IntegrityValidationTest, BothHashesAreOkPickLatest) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.secondary->generation = metadata.primary->generation + 1;
  metadata.UpdateHash(SuperblockType::kPrimary);
  metadata.UpdateHash(SuperblockType::kSecondary);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kSecondary, *result);
}

TEST(IntegrityValidationTest, PrimaryIsOkAndSecondaryIsCorruptedPicksPrimary) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.secondary->fvm_partition_size = 0;
  metadata.UpdateHash(SuperblockType::kPrimary);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kPrimary, *result);
}

TEST(IntegrityValidationTest, PrimaryIsCorruptedAndSecondaryIsOkPicksSecondary) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->fvm_partition_size = 0;
  metadata.UpdateHash(SuperblockType::kSecondary);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kSecondary, *result);
}

TEST(IntegrityValidationTest, BothAreCorruptedIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->fvm_partition_size = 0;
  metadata.secondary->fvm_partition_size = 0;

  EXPECT_FALSE(metadata.ValidateHeader());
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnPrimaryPicksSecondary) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->allocation_table_size = 0;
  metadata.UpdateHash(SuperblockType::kSecondary);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kSecondary, *result);
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnSecondaryPicksPrimary) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.secondary->allocation_table_size = 0;
  metadata.UpdateHash(SuperblockType::kPrimary);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kPrimary, *result);
}

TEST(IntegrityValidationTest, ReportedMetadataSizeIsTooSmallOnBothIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->allocation_table_size = 0;
  metadata.secondary->allocation_table_size = 0;

  EXPECT_FALSE(metadata.ValidateHeader());
}

TEST(IntegrityValidationTest, ValidatesMetadataSizeNotCapacity) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.UpdateHash(SuperblockType::kPrimary);

  // Set the unused portions of the primary partition to 1. This is not taken into account when
  // validating the metadata header, we only check the data we are actually using.
  std::fill(metadata.primary_buffer.begin() + metadata.primary->GetMetadataUsedBytes(),
            metadata.primary_buffer.end(), 1);

  auto result = metadata.ValidateHeader();
  ASSERT_TRUE(result);
  EXPECT_EQ(SuperblockType::kPrimary, *result);
}

TEST(IntegrityValidationTest, ZeroedHeaderIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  std::fill(metadata.primary_buffer.begin(), metadata.primary_buffer.end(), 0);
  std::fill(metadata.secondary_buffer.begin(), metadata.secondary_buffer.end(), 0);

  EXPECT_FALSE(metadata.ValidateHeader());
}

TEST(IntegrityValidationTest, MetadataHasOverflowInCalculatedSizeIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->allocation_table_size =
      std::numeric_limits<uint64_t>::max() - metadata.primary->GetAllocationTableOffset() + 1;

  EXPECT_FALSE(metadata.ValidateHeader());
}

TEST(IntegrityValidationTest, FvmPartitionNotBigForBothCopiesOfMetadataIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);
  metadata.primary->fvm_partition_size = metadata.primary->GetDataStartOffset() - 1;
  metadata.secondary->fvm_partition_size = metadata.primary->fvm_partition_size;

  EXPECT_FALSE(metadata.ValidateHeader());
}

TEST(IntegrityValidationTest, LastSliceOutOfFvmPartitionIsBadState) {
  Metadata metadata = CreateMetadata(kPartitionSize, 2 * kPartitionSize);

  // Now the last slice ends past the fvm partition and would trigger a Page Fault, probably.
  metadata.primary->fvm_partition_size = metadata.primary->GetSliceDataOffset(
      metadata.primary->GetAllocationTableUsedEntryCount() - 1);
  metadata.secondary->fvm_partition_size = metadata.primary->fvm_partition_size;

  EXPECT_FALSE(metadata.ValidateHeader());
}

}  // namespace
}  // namespace fvm
