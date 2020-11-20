// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <random>
#include <vector>

#include <fbl/span.h>
#include <fvm/snapshot-metadata-format.h>
#include <fvm/snapshot-metadata.h>
#include <zxtest/zxtest.h>

namespace fvm {

static std::default_random_engine rand(::zxtest::Runner::GetInstance()->random_seed());

SnapshotExtentType CreateExtentTypeEntry(uint16_t vpart) {
  static uint64_t gVslice = 0;
  std::uniform_int_distribution<uint64_t> d1(1024u);
  uint64_t extent_length = d1(rand);
  uint64_t vslice = gVslice;
  gVslice += extent_length;
  std::uniform_int_distribution<uint8_t> d2(static_cast<uint8_t>(ExtentType::kDefault),
                                            static_cast<uint8_t>(ExtentType::kMax));
  ExtentType type = static_cast<ExtentType>(d2(rand));
  return SnapshotExtentType(vpart, vslice, extent_length, type);
}

PartitionSnapshotState CreatePartitionStateEntry() { return PartitionSnapshotState(); }

void ValidateMetadata(const SnapshotMetadata& metadata,
                      const std::vector<SnapshotExtentType>& expected_extents) {
  const SnapshotMetadataHeader& header = metadata.GetHeader();

  ASSERT_GE(header.ExtentTypeTableNumEntries(), expected_extents.size());
  unsigned i = 0;
  for (; i < expected_extents.size(); ++i) {
    EXPECT_BYTES_EQ(&expected_extents[i], &metadata.GetExtentTypeEntry(i),
                    sizeof(SnapshotExtentType));
  }
  for (; i < header.ExtentTypeTableNumEntries(); ++i) {
    EXPECT_TRUE(metadata.GetExtentTypeEntry(i).IsFree());
  }
}

void CheckMetadataContainSameEntries(const SnapshotMetadata& a, const SnapshotMetadata& b) {
  const SnapshotMetadataHeader& header_a = a.GetHeader();
  const SnapshotMetadataHeader& header_b = b.GetHeader();

  unsigned i = 0, j = 0;
  for (; i < header_a.ExtentTypeTableNumEntries() && j < header_b.ExtentTypeTableNumEntries();
       ++i, ++j) {
    EXPECT_BYTES_EQ(&a.GetExtentTypeEntry(i), &b.GetExtentTypeEntry(j), sizeof(SnapshotExtentType));
  }
  for (; i < header_a.ExtentTypeTableNumEntries(); ++i) {
    EXPECT_TRUE(a.GetExtentTypeEntry(i).IsFree());
  }
  for (; j < header_b.ExtentTypeTableNumEntries(); ++j) {
    EXPECT_TRUE(b.GetExtentTypeEntry(j).IsFree());
  }
}

TEST(ValidateMetadata, DefaultValid) {
  SnapshotMetadataHeader header;

  std::string unused;
  ASSERT_TRUE(header.IsValid(unused));
}

TEST(ValidateMetadata, BadMagicFails) {
  SnapshotMetadataHeader header;
  header.magic = 0u;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, BadPartitionStateTableSizeFails) {
  SnapshotMetadataHeader header;
  header.partition_state_table_entry_count = kSnapshotMetadataHeaderMinPartitions - 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));

  header.partition_state_table_entry_count = kSnapshotMetadataHeaderMaxPartitions + 1;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, BadExtentTypeTableSizeFails) {
  SnapshotMetadataHeader header;
  header.extent_type_table_entry_count = kSnapshotMetadataHeaderMinExtentTypes - 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));

  header.extent_type_table_entry_count = kSnapshotMetadataHeaderMaxExtentTypes + 1;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, PartitionStateTableOverlapsHeaderFails) {
  SnapshotMetadataHeader header;
  header.partition_state_table_offset = sizeof(SnapshotMetadataHeader) - 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, ExtentTypeTableOverlapsHeaderFails) {
  SnapshotMetadataHeader header;
  header.extent_type_table_offset = sizeof(SnapshotMetadataHeader) - 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, ExtentTypeTableOverlapsPartitionStateTableFails) {
  SnapshotMetadataHeader header;
  header.extent_type_table_offset =
      header.partition_state_table_offset + header.PartitionStateTableSizeBytes() - 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(ValidateMetadata, ExtentTypeTableOverlapsSecondHeaderFails) {
  SnapshotMetadataHeader header;
  header.extent_type_table_offset =
      header.HeaderOffset(SnapshotMetadataCopy::kSecondary) - header.ExtentTypeTableSizeBytes() + 1;

  std::string unused;
  ASSERT_FALSE(header.IsValid(unused));
}

TEST(CreateMetadata, Empty) {
  auto result = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result.status_value(), ZX_OK);
  ValidateMetadata(result.value(), std::vector<SnapshotExtentType>());
}

TEST(CreateMetadata, OneExtent) {
  std::vector<SnapshotExtentType> extents{CreateExtentTypeEntry(1u)};

  auto result = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  ASSERT_EQ(result.status_value(), ZX_OK);
  ValidateMetadata(result.value(), extents);
}

TEST(CreateMetadata, SeveralExtents) {
  std::vector<SnapshotExtentType> extents{
      CreateExtentTypeEntry(1u),
      CreateExtentTypeEntry(1u),
      CreateExtentTypeEntry(2u),
  };

  auto result = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  ASSERT_EQ(result.status_value(), ZX_OK);
  ValidateMetadata(result.value(), extents);
}

TEST(CreateMetadata, FullExtentTable) {
  std::vector<SnapshotExtentType> extents(kSnapshotMetadataHeaderMaxExtentTypes);
  std::generate(extents.begin(), extents.end(), []() { return CreateExtentTypeEntry(1u); });

  auto result = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  ASSERT_EQ(result.status_value(), ZX_OK);
  ValidateMetadata(result.value(), extents);
}

TEST(CreateMetadata, TooManyExtents) {
  std::vector<SnapshotExtentType> extents(kSnapshotMetadataHeaderMaxExtentTypes + 1);
  std::generate(extents.begin(), extents.end(), []() { return CreateExtentTypeEntry(1u); });

  auto result = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  ASSERT_NE(result.status_value(), ZX_OK);
}

TEST(Metadata, HeaderOffsets) {
  auto result = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result.status_value(), ZX_OK);

  ASSERT_EQ(result->active_header(), SnapshotMetadataCopy::kPrimary);
  ASSERT_EQ(result->GetInactiveHeaderOffset(), kSnapshotMetadataSecondHeaderOffset);

  result->SwitchActiveHeaders();

  ASSERT_EQ(result->active_header(), SnapshotMetadataCopy::kSecondary);
  ASSERT_EQ(result->GetInactiveHeaderOffset(), 0u);
}

TEST(PickValidMetadata, BothValidTakesFirst) {
  std::vector<SnapshotExtentType> extents{CreateExtentTypeEntry(1u)};
  auto result1 = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  auto result2 = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result1.status_value(), ZX_OK);
  ASSERT_EQ(result2.status_value(), ZX_OK);
  ASSERT_EQ(result1->Get()->size(), result2->Get()->size());

  std::optional<SnapshotMetadataCopy> copy = SnapshotMetadata::PickValid(
      result1->Get()->data(), result2->Get()->data(), result1->Get()->size());
  ASSERT_TRUE(copy);
  EXPECT_EQ(*copy, SnapshotMetadataCopy::kPrimary);
}

TEST(PickValidMetadata, FirstInvalid) {
  std::vector<SnapshotExtentType> extents{CreateExtentTypeEntry(1u)};
  auto result1 = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  auto result2 = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result1.status_value(), ZX_OK);
  ASSERT_EQ(result2.status_value(), ZX_OK);
  ASSERT_EQ(result1->Get()->size(), result2->Get()->size());
  // Zero out the magic
  bzero(result1->Get()->data(), sizeof(SnapshotMetadataHeader::magic));

  std::optional<SnapshotMetadataCopy> copy = SnapshotMetadata::PickValid(
      result1->Get()->data(), result2->Get()->data(), result1->Get()->size());
  ASSERT_TRUE(copy);
  EXPECT_EQ(*copy, SnapshotMetadataCopy::kSecondary);
}

TEST(PickValidMetadata, SecondInvalid) {
  std::vector<SnapshotExtentType> extents{CreateExtentTypeEntry(1u)};
  auto result1 = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  auto result2 = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result1.status_value(), ZX_OK);
  ASSERT_EQ(result2.status_value(), ZX_OK);
  ASSERT_EQ(result1->Get()->size(), result2->Get()->size());
  // Zero out the magic
  bzero(result2->Get()->data(), sizeof(SnapshotMetadataHeader::magic));

  std::optional<SnapshotMetadataCopy> copy = SnapshotMetadata::PickValid(
      result1->Get()->data(), result2->Get()->data(), result1->Get()->size());
  ASSERT_TRUE(copy);
  EXPECT_EQ(*copy, SnapshotMetadataCopy::kPrimary);
}

TEST(PickValidMetadata, BothInvalid) {
  std::vector<SnapshotExtentType> extents{CreateExtentTypeEntry(1u)};
  auto result1 = SnapshotMetadata::Synthesize(nullptr, 0, extents.data(), extents.size());
  auto result2 = SnapshotMetadata::Synthesize(nullptr, 0, nullptr, 0);
  ASSERT_EQ(result1.status_value(), ZX_OK);
  ASSERT_EQ(result2.status_value(), ZX_OK);
  ASSERT_EQ(result1->Get()->size(), result2->Get()->size());
  // Zero out the magic
  bzero(result1->Get()->data(), sizeof(SnapshotMetadataHeader::magic));
  bzero(result2->Get()->data(), sizeof(SnapshotMetadataHeader::magic));

  std::optional<SnapshotMetadataCopy> copy = SnapshotMetadata::PickValid(
      result1->Get()->data(), result2->Get()->data(), result1->Get()->size());
  ASSERT_FALSE(copy);
}

}  // namespace fvm
