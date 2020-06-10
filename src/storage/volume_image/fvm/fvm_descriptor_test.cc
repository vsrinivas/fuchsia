// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <string_view>

#include <fbl/algorithm.h>
#include <fvm/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/fvm/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

using internal::GetMetadataSize;

CompressionOptions Lz4Compression() {
  CompressionOptions compression = {};
  compression.schema = CompressionSchema::kLz4;
  return compression;
}

FvmOptions ValidOptions() {
  FvmOptions options = {};
  options.compression = Lz4Compression();
  options.slice_size = 8192;
  return options;
}

Partition MakePartitionWithNameAndInstanceGuid(
    std::string_view name, const std::array<uint8_t, kGuidLength>& instance_guid,
    uint64_t block_size, uint64_t block_count) {
  VolumeDescriptor volume = {};
  ZX_ASSERT(name.size() < kNameLength);
  volume.name = name;
  volume.instance = instance_guid;
  volume.block_size = block_size;
  AddressDescriptor address = {};
  AddressMap mapping = {};
  mapping.count = block_count - 2;
  mapping.source = 0;
  mapping.target = 0;
  address.mappings.push_back(mapping);
  mapping.count = 2;
  mapping.source = 2 * block_count;
  mapping.target = 10 * block_count;
  address.mappings.push_back(mapping);
  return Partition(volume, address, nullptr);
}

void CheckPartition(std::string_view name, const std::array<uint8_t, kGuidLength>& guid,
                    uint64_t block_size, uint64_t block_count, const Partition& partition) {
  EXPECT_EQ(name, partition.volume().name);
  EXPECT_EQ(guid, partition.volume().instance);
  EXPECT_EQ(block_size, partition.volume().block_size);

  ASSERT_EQ(2u, partition.address().mappings.size());

  EXPECT_EQ(block_count - 2, partition.address().mappings[0].count);
  EXPECT_EQ(0u, partition.address().mappings[0].source);
  EXPECT_EQ(0u, partition.address().mappings[0].target);

  EXPECT_EQ(2u, partition.address().mappings[1].count);
  EXPECT_EQ(2 * block_count, partition.address().mappings[1].source);
  EXPECT_EQ(10 * block_count, partition.address().mappings[1].target);
}

TEST(FvmDescriptorBuilderTest, ConstructFromDescriptorIsOk) {
  FvmOptions options = ValidOptions();
  auto guid = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid.is_ok()) << guid.error();

  // Metadata for the fvm should get this past the upper bound of target size.
  Partition partition =
      MakePartitionWithNameAndInstanceGuid("Partition-1", guid.value(), options.slice_size, 20);
  FvmDescriptor::Builder builder;
  auto descriptor_result = builder.SetOptions(options).AddPartition(std::move(partition)).Build();
  ASSERT_TRUE(descriptor_result.is_ok()) << descriptor_result.error();
  FvmDescriptor descriptor = descriptor_result.take_value();
  builder = FvmDescriptor::Builder(std::move(descriptor));
  auto result = builder.Build();
  ASSERT_TRUE(result.is_ok()) << result.error();

  EXPECT_EQ(options.compression.schema, result.value().options().compression.schema);
  EXPECT_EQ(options.compression.options, result.value().options().compression.options);
  EXPECT_EQ(options.max_volume_size, result.value().options().max_volume_size);
  EXPECT_EQ(options.target_volume_size, result.value().options().target_volume_size);
  EXPECT_EQ(options.slice_size, result.value().options().slice_size);

  ASSERT_EQ(1u, result.value().partitions().size());
  EXPECT_EQ(20u, result.value().slice_count());
  EXPECT_GT(result.value().metadata_required_size(), 0u);
  CheckPartition("Partition-1", guid.value(), options.slice_size, 20,
                 *result.value().partitions().begin());
}

TEST(FvmDescriptorBuilderTest, BuildWithoutOptionsIsError) {
  FvmDescriptor::Builder builder;
  ASSERT_TRUE(builder.Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithZeroSizeSliceIsError) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  options.slice_size = 0;
  ASSERT_TRUE(builder.SetOptions(options).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithMaxVolumeSizeSmallerThanTargetSizeIsError) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  options.target_volume_size = options.slice_size * 100;
  options.max_volume_size = options.target_volume_size.value() - 1;
  ASSERT_TRUE(builder.SetOptions(options).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWhenSizeIsBiggerThanTargetSizeIsError) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  options.target_volume_size = options.slice_size * 20;
  options.max_volume_size = options.target_volume_size.value() * 4;

  auto guid = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid.is_ok()) << guid.error();
  // Metadata for the fvm should get this past the upper bound of target size.
  Partition partition =
      MakePartitionWithNameAndInstanceGuid("Partition-1", guid.value(), options.slice_size, 20);
  builder.AddPartition(std::move(partition));

  ASSERT_TRUE(builder.SetOptions(options).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithDuplicatedPartitionsIsError) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  options.target_volume_size = options.slice_size * 20;
  options.max_volume_size = options.target_volume_size.value() * 4;

  auto guid = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid.is_ok()) << guid.error();
  // Metadata for the fvm should get this past the upper bound of target size.
  Partition partition_1 =
      MakePartitionWithNameAndInstanceGuid("Partition-1", guid.value(), options.slice_size, 20);
  Partition partition_2 =
      MakePartitionWithNameAndInstanceGuid("Partition-1", guid.value(), options.slice_size, 20);
  builder.AddPartition(std::move(partition_1)).AddPartition(std::move(partition_2));

  ASSERT_TRUE(builder.SetOptions(options).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithTargetVolumeSizeOnlyIsOk) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  options.target_volume_size = options.slice_size * 100;
  options.max_volume_size = std::nullopt;
  ASSERT_TRUE(builder.SetOptions(options).Build().is_ok());
}

TEST(FvmDescriptorBuilderTest, BuildWithNoPartitionsIsOk) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();
  auto result = builder.SetOptions(options).Build();
  ASSERT_TRUE(result.is_ok());
  auto fvm_descriptor = result.take_value();

  EXPECT_TRUE(fvm_descriptor.partitions().empty());
  EXPECT_EQ(options.compression.schema, fvm_descriptor.options().compression.schema);
  EXPECT_EQ(options.compression.options, fvm_descriptor.options().compression.options);
  EXPECT_EQ(options.max_volume_size, fvm_descriptor.options().max_volume_size);
  EXPECT_EQ(options.target_volume_size, fvm_descriptor.options().target_volume_size);
  EXPECT_EQ(options.slice_size, fvm_descriptor.options().slice_size);
  EXPECT_EQ(0u, fvm_descriptor.slice_count());
  EXPECT_EQ(GetMetadataSize(options, 0), fvm_descriptor.metadata_required_size());
}

TEST(FvmDescriptorBuilderTest, BuildWithDifferentPartitionsIsOk) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();

  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();

  auto guid_2 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E7C"));
  ASSERT_TRUE(guid_2.is_ok()) << guid_2.error();

  auto guid_3 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E7C"));
  ASSERT_TRUE(guid_3.is_ok()) << guid_3.error();

  // Metadata for the fvm should get this past the upper bound of target size.
  Partition partition_1 =
      MakePartitionWithNameAndInstanceGuid("Partition-1", guid_1.value(), options.slice_size, 20);
  Partition partition_2 = MakePartitionWithNameAndInstanceGuid("Partition-1", guid_2.value(),
                                                               options.slice_size / 2, 20);
  Partition partition_3 = MakePartitionWithNameAndInstanceGuid("Partition-2", guid_3.value(),
                                                               options.slice_size / 2, 20);

  auto result = builder.AddPartition(std::move(partition_1))
                    .AddPartition(std::move(partition_2))
                    .AddPartition(std::move(partition_3))
                    .SetOptions(options)
                    .Build();
  ASSERT_TRUE(result.is_ok());
  auto fvm_descriptor = result.take_value();

  EXPECT_EQ(options.compression.schema, fvm_descriptor.options().compression.schema);
  EXPECT_EQ(options.compression.options, fvm_descriptor.options().compression.options);
  EXPECT_EQ(options.max_volume_size, fvm_descriptor.options().max_volume_size);
  EXPECT_EQ(options.target_volume_size, fvm_descriptor.options().target_volume_size);
  EXPECT_EQ(options.slice_size, fvm_descriptor.options().slice_size);
  EXPECT_EQ(40u, fvm_descriptor.slice_count());
  EXPECT_EQ(GetMetadataSize(options, 40), fvm_descriptor.metadata_required_size());

  const auto& partitions = fvm_descriptor.partitions();
  ASSERT_EQ(3u, partitions.size());

  auto partition = partitions.begin();
  CheckPartition("Partition-1", guid_1.value(), options.slice_size, 20, *partition);

  ++partition;
  CheckPartition("Partition-1", guid_2.value(), options.slice_size / 2, 20, *partition);

  ++partition;
  CheckPartition("Partition-2", guid_3.value(), options.slice_size / 2, 20, *partition);
}

TEST(FvmDescriptorBuilderTest, BuildWitPartitionsWithTailInMappingsIsOk) {
  FvmDescriptor::Builder builder;
  auto options = ValidOptions();

  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();

  // This will produce 2 mappings with 19 half slices and other with 2, which should require 11
  // total slices.
  Partition partition_1 = MakePartitionWithNameAndInstanceGuid("Partition-1", guid_1.value(),
                                                               options.slice_size / 2, 21);

  auto result = builder.AddPartition(std::move(partition_1)).SetOptions(options).Build();
  ASSERT_TRUE(result.is_ok()) << result.error();

  ASSERT_EQ(11u, result.value().slice_count());
  const auto& partitions = result.value().partitions();
  ASSERT_EQ(1u, partitions.size());

  auto partition = partitions.begin();
  CheckPartition("Partition-1", guid_1.value(), options.slice_size / 2, 21, *partition);
}

TEST(GetMetadataSizeTest, AccountsForTwoCopiesOfMetadata) {
  constexpr unsigned int kSliceCount = 33;

  auto options = ValidOptions();
  options.slice_size = 1 << 20;

  ASSERT_EQ(2 * (fvm::AllocationTable::kOffset +
                 fbl::round_up(kSliceCount * sizeof(fvm::SliceEntry), fvm::kBlockSize)),
            internal::GetMetadataSize(options, kSliceCount));
}

TEST(GetMetadataSizeTest, AllocatesSpaceForMaxVolumeSizeWhenSet) {
  constexpr unsigned int kSliceCount = 33;

  auto options = ValidOptions();
  options.slice_size = 1ull << 20;
  options.max_volume_size = 10 * (1ull << 30);
  options.target_volume_size = 5 * (1ull << 25);

  ASSERT_EQ(fvm::MetadataSize(*options.max_volume_size, options.slice_size),
            internal::GetMetadataSize(options, kSliceCount));
}

TEST(GetMetadataSizeTest, AllocatesSpaceForTargetVolumeSizeWhenSet) {
  constexpr unsigned int kSliceCount = 33;

  auto options = ValidOptions();
  options.slice_size = 1ull << 20;
  options.max_volume_size = std::nullopt;
  options.target_volume_size = 5 * (1ull << 25);

  ASSERT_EQ(fvm::MetadataSize(*options.target_volume_size, options.slice_size),
            internal::GetMetadataSize(options, kSliceCount));
}

}  // namespace
}  // namespace storage::volume_image
