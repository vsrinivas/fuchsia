// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <string_view>

#include <fbl/algorithm.h>
#include <fvm/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

// Returns the expected total size of the metadata requied for both copies at the beginning of the
// volume.
uint64_t GetMetadataSize(const FvmOptions& options, uint64_t slice_count) {
  return 2 * internal::MakeHeader(options, slice_count).GetMetadataAllocatedBytes();
}

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
  mapping.count = (block_count - 2) * block_size;
  mapping.source = 0;
  mapping.target = 0;
  address.mappings.push_back(mapping);
  mapping.count = 2 * block_size;
  mapping.source = 2 * block_count * block_size;
  mapping.target = 10 * block_count * block_size;
  address.mappings.push_back(mapping);
  return Partition(volume, address, nullptr);
}

void CheckPartition(std::string_view name, const std::array<uint8_t, kGuidLength>& guid,
                    uint64_t block_size, uint64_t block_count, const Partition& partition) {
  EXPECT_EQ(partition.volume().name, name);
  EXPECT_EQ(partition.volume().instance, guid);
  EXPECT_EQ(partition.volume().block_size, block_size);

  ASSERT_EQ(partition.address().mappings.size(), 2u);

  EXPECT_EQ(partition.address().mappings[0].count, (block_count - 2) * block_size);
  EXPECT_EQ(partition.address().mappings[0].source, 0u);
  EXPECT_EQ(partition.address().mappings[0].target, 0u);

  EXPECT_EQ(partition.address().mappings[1].count, 2u * block_size);
  EXPECT_EQ(partition.address().mappings[1].source, 2 * block_count * block_size);
  EXPECT_EQ(partition.address().mappings[1].target, 10 * block_count * block_size);
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

  EXPECT_EQ(result.value().options().compression.schema, options.compression.schema);
  EXPECT_EQ(result.value().options().compression.options, options.compression.options);
  EXPECT_EQ(result.value().options().max_volume_size, options.max_volume_size);
  EXPECT_EQ(result.value().options().target_volume_size, options.target_volume_size);
  EXPECT_EQ(result.value().options().slice_size, options.slice_size);

  ASSERT_EQ(result.value().partitions().size(), 1u);
  EXPECT_EQ(result.value().slice_count(), 20u);
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
  EXPECT_EQ(fvm_descriptor.options().compression.schema, options.compression.schema);
  EXPECT_EQ(fvm_descriptor.options().compression.options, options.compression.options);
  EXPECT_EQ(fvm_descriptor.options().max_volume_size, options.max_volume_size);
  EXPECT_EQ(fvm_descriptor.options().target_volume_size, options.target_volume_size);
  EXPECT_EQ(fvm_descriptor.options().slice_size, options.slice_size);
  EXPECT_EQ(fvm_descriptor.slice_count(), 0u);
  EXPECT_EQ(fvm_descriptor.metadata_required_size(), GetMetadataSize(options, 0));
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

  EXPECT_EQ(fvm_descriptor.options().compression.schema, options.compression.schema);
  EXPECT_EQ(fvm_descriptor.options().compression.options, options.compression.options);
  EXPECT_EQ(fvm_descriptor.options().max_volume_size, options.max_volume_size);
  EXPECT_EQ(fvm_descriptor.options().target_volume_size, options.target_volume_size);
  EXPECT_EQ(fvm_descriptor.options().slice_size, options.slice_size);
  EXPECT_EQ(fvm_descriptor.slice_count(), 40u);
  EXPECT_EQ(fvm_descriptor.metadata_required_size(), GetMetadataSize(options, 40));

  const auto& partitions = fvm_descriptor.partitions();
  ASSERT_EQ(partitions.size(), 3u);

  auto partition = partitions.begin();
  CheckPartition("Partition-1", guid_1.value(), options.slice_size, 20, *partition);

  ++partition;
  CheckPartition("Partition-1", guid_2.value(), options.slice_size / 2, 20, *partition);

  ++partition;
  CheckPartition("Partition-2", guid_3.value(), options.slice_size / 2, 20, *partition);
}

TEST(FvmDescriptorBuilderTest, BuildWithPartitionsWithTailInMappingsIsOk) {
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

  ASSERT_EQ(result.value().slice_count(), 11u);
  const auto& partitions = result.value().partitions();
  ASSERT_EQ(partitions.size(), 1u);

  auto partition = partitions.begin();
  CheckPartition("Partition-1", guid_1.value(), options.slice_size / 2, 21, *partition);
}

TEST(FvmDescriptorBuilderTest, BuildWithOverlapingUnalignedMappingsIsError) {
  VolumeDescriptor descriptor;
  descriptor.name = "1";
  AddressDescriptor address_descriptor;
  address_descriptor.mappings.push_back({.source = 40, .target = 0, .count = 10});
  address_descriptor.mappings.push_back({.source = 40, .target = 5, .count = 5});
  Partition partition(descriptor, address_descriptor, nullptr);

  ASSERT_TRUE(FvmDescriptor::Builder().AddPartition(std::move(partition)).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithOverlapingAlignedMappingsIsError) {
  VolumeDescriptor descriptor;
  descriptor.name = "1";
  AddressDescriptor address_descriptor;
  address_descriptor.mappings.push_back({.source = 40, .target = 0, .count = 10});
  address_descriptor.mappings.push_back({.source = 40, .target = 0, .count = 5});
  Partition partition(descriptor, address_descriptor, nullptr);
  ASSERT_TRUE(FvmDescriptor::Builder().AddPartition(std::move(partition)).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithOverlapingShiftedMappingsIsError) {
  VolumeDescriptor descriptor;
  descriptor.name = "1";
  AddressDescriptor address_descriptor;
  address_descriptor.mappings.push_back({.source = 40, .target = 0, .count = 22});
  address_descriptor.mappings.push_back({.source = 40, .target = 20, .count = 5});
  Partition partition(descriptor, address_descriptor, nullptr);
  ASSERT_TRUE(FvmDescriptor::Builder().AddPartition(std::move(partition)).Build().is_error());
}

TEST(FvmDescriptorBuilderTest, BuildWithContiguousAlignedMappingsIsOk) {
  VolumeDescriptor descriptor;
  descriptor.name = "1";
  AddressDescriptor address_descriptor;
  // Protect ourselves by an off by 1.
  address_descriptor.mappings.push_back({.source = 40, .target = 0, .count = 10});
  address_descriptor.mappings.push_back({.source = 40, .target = 10, .count = 5});
  address_descriptor.mappings.push_back({.source = 40, .target = 15, .count = 5});
  Partition partition(descriptor, address_descriptor, nullptr);
  ASSERT_TRUE(FvmDescriptor::Builder().AddPartition(std::move(partition)).Build().is_error());
}

TEST(FvmDescriptor, MakeHeader) {
  // Use a very small slice count and size so the answers from the three different computations
  // will vary significantly. Various tables in FVM can be rounded up so this test doesn't test
  // exact values, only that things are in the correct range.
  constexpr uint64_t kSliceSize = fvm::kBlockSize;
  constexpr uint64_t kSliceCount = 2;
  constexpr uint64_t kMaxSize = 10 * (1ull << 30);
  constexpr uint64_t kTargetSize = 5 * (1ull << 25);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  // Max size is used if it's set.
  fvm::Header header = internal::MakeHeader(options, kSliceCount);
  EXPECT_GE(header.fvm_partition_size, kMaxSize);

  // The target size should be used if the max size isn't set.
  options.max_volume_size = std::nullopt;
  header = internal::MakeHeader(options, kSliceCount);
  EXPECT_GE(header.fvm_partition_size, kTargetSize);
  EXPECT_LT(header.fvm_partition_size, kMaxSize);

  // The slice count should be used if nothing else is set.
  options.target_volume_size = std::nullopt;
  header = internal::MakeHeader(options, kSliceCount);
  constexpr uint64_t kExpectedPartitionSize = kSliceSize * kSliceCount;
  EXPECT_GE(header.fvm_partition_size, kExpectedPartitionSize);
  EXPECT_LT(header.fvm_partition_size, kTargetSize);
}

}  // namespace
}  // namespace storage::volume_image
