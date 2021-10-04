// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_descriptor.h"

#include <lib/fit/function.h>
#include <string.h>
#include <zircon/assert.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata.h"
#include "src/storage/fvm/metadata_buffer.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/utils/reader.h"
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

TEST(FvmDescriptorTest, MakeHeader) {
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

  // Max size is used for allocated data.
  fvm::Header header = internal::MakeHeader(options, kSliceCount);
  EXPECT_GE(header.fvm_partition_size, kTargetSize);
  EXPECT_LT(header.fvm_partition_size, kMaxSize);
  auto expected = fvm::Header::FromDiskSize(fvm::kMaxVPartitions - 1, kMaxSize, kSliceSize);
  EXPECT_EQ(header.GetAllocationTableAllocatedEntryCount(),
            expected.GetAllocationTableAllocatedEntryCount());

  // The target size should be used if the max size isn't set.
  options.max_volume_size = std::nullopt;
  header = internal::MakeHeader(options, kSliceCount);
  EXPECT_GE(header.fvm_partition_size, kTargetSize);
  EXPECT_LT(header.fvm_partition_size, kMaxSize);
  expected = fvm::Header::FromDiskSize(fvm::kMaxVPartitions - 1, kTargetSize, kSliceSize);
  EXPECT_EQ(header.GetAllocationTableAllocatedEntryCount(),
            expected.GetAllocationTableAllocatedEntryCount());

  // The slice count should be used if nothing else is set.
  options.target_volume_size = std::nullopt;
  header = internal::MakeHeader(options, kSliceCount);
  constexpr uint64_t kExpectedPartitionSize = kSliceSize * kSliceCount;
  EXPECT_GE(header.fvm_partition_size, kExpectedPartitionSize);
  EXPECT_LT(header.fvm_partition_size, kTargetSize);
}

template <int shift>
fpromise::result<void, std::string> GetContents(uint64_t offset, cpp20::span<uint8_t> buffer) {
  for (uint64_t index = 0; index < buffer.size(); ++index) {
    buffer[index] = (offset + index + shift) % std::numeric_limits<uint8_t>::max();
  }
  return fpromise::ok();
}

class FakeReader final : public Reader {
 public:
  FakeReader(fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)>
                 content_provider,
             uint64_t length)
      : content_provider_(std::move(content_provider)), length_(length) {}
  explicit FakeReader(
      fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)>
          content_provider)
      : FakeReader(std::move(content_provider), std::numeric_limits<uint64_t>::max()) {}

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (buffer.size() > length_ || offset > length_ - buffer.size()) {
      return fpromise::error("FakeReader::Read Out of Range. Offset: " + std::to_string(offset) +
                             " buffer size: " + std::to_string(buffer.size()) +
                             " length: " + std::to_string(length_));
    }
    return content_provider_(offset, buffer);
  }

  uint64_t length() const final { return length_; }

 private:
  fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)>
      content_provider_;
  uint64_t length_;
};

// Creates a partition instance with the target properties.
//
// Precondition: |block_count| > 20.
Partition MakePartitionWithNameAndInstanceGuidAndContentProvider(
    std::string_view name, const std::array<uint8_t, kGuidLength>& type_guid,
    const std::array<uint8_t, kGuidLength>& instance_guid, uint64_t block_size,
    uint64_t block_count,
    fit::function<fpromise::result<void, std::string>(uint64_t, cpp20::span<uint8_t>)>
        content_provider) {
  VolumeDescriptor volume = {};
  ZX_ASSERT(name.size() < kNameLength);
  volume.name = name;
  volume.type = type_guid;
  volume.instance = instance_guid;
  volume.block_size = block_size;
  AddressDescriptor address = {};
  AddressMap mapping = {};
  mapping.count = (block_count - 20) * block_size;
  mapping.source = 0;
  mapping.target = 0;
  address.mappings.push_back(mapping);
  mapping.source = 8;
  mapping.count = 4 * block_size;
  mapping.size = 20 * block_size;
  mapping.target = 10 * block_count * block_size;
  mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
  address.mappings.push_back(mapping);
  return Partition(
      volume, address,
      std::make_unique<FakeReader>(std::move(content_provider), block_count * block_size));
}
class ErrorWriter final : public Writer {
  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    return fpromise::error("Oops something went wrong!.");
  }
};

class FakeWriter final : public Writer {
 public:
  // Like writing into a file, the intermediate unwritten parts are zeroed,
  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final {
    if (offset + buffer.size() > data_.size()) {
      data_.resize(offset + buffer.size(), 0);
    }
    memcpy(&data_[offset], buffer.data(), buffer.size());
    return fpromise::ok();
  }

  // So we can peek at the end result.
  auto& data() { return data_; }

 private:
  std::vector<uint8_t> data_;
};

// Conforms to a the MetadataBuffer interface required, and allows to inject and unowned buffer if
// necessary. Why is useful for testing.
class MetadataBufferView final : public fvm::MetadataBuffer {
 public:
  MetadataBufferView() : data_(std::vector<uint8_t>()) {}
  explicit MetadataBufferView(cpp20::span<uint8_t> data) : data_(data) {}

  std::unique_ptr<MetadataBuffer> Create(size_t size) const final {
    auto view = std::make_unique<MetadataBufferView>();
    std::get<std::vector<uint8_t>>(view->data_).resize(size);
    return std::move(view);
  }

  void* data() const final {
    return std::visit([](auto& a) { return static_cast<void*>(a.data()); }, data_);
  }

  size_t size() const final {
    return std::visit([](auto& a) { return a.size(); }, data_);
  }

 private:
  mutable std::variant<cpp20::span<uint8_t>, std::vector<uint8_t>> data_;
};

void CheckPartitionMetadata(const FvmDescriptor& descriptor, fvm::Metadata& metadata) {
  uint64_t allocated_partitions = descriptor.partitions().size();
  auto expected_partition_it = descriptor.partitions().begin();

  uint64_t current_physical_slice = 1;
  for (uint64_t partition_index = 1; partition_index <= allocated_partitions; ++partition_index) {
    const auto& partition_descriptor = *expected_partition_it;
    const auto& partition_entry = metadata.GetPartitionEntry(partition_index);

    EXPECT_EQ(partition_entry.name(), partition_descriptor.volume().name);
    EXPECT_THAT(partition_entry.guid,
                testing::ElementsAreArray(partition_descriptor.volume().instance));
    EXPECT_THAT(partition_entry.type,
                testing::ElementsAreArray(partition_descriptor.volume().type));
    EXPECT_TRUE(partition_entry.IsAllocated());
    EXPECT_TRUE(partition_entry.IsActive());

    uint64_t accumulated_slice_count_per_partition = 0;
    for (const auto& mapping : partition_descriptor.address().mappings) {
      uint64_t size = std::max(mapping.count, mapping.size.value_or(0));
      uint64_t allocated_slice_count_per_extent =
          GetBlockCount(mapping.target, size, descriptor.options().slice_size);

      for (uint64_t pslice = 0; pslice < allocated_slice_count_per_extent; ++pslice) {
        const auto& slice_entry = metadata.GetSliceEntry(current_physical_slice + pslice);
        EXPECT_EQ(slice_entry.VPartition(), partition_index);
        // Calculate vslice start.
        uint64_t vslice =
            GetBlockFromBytes(mapping.target + pslice * descriptor.options().slice_size,
                              descriptor.options().slice_size);
        EXPECT_EQ(slice_entry.VSlice(), vslice);
      }
      current_physical_slice += allocated_slice_count_per_extent;
      accumulated_slice_count_per_partition += allocated_slice_count_per_extent;
    }
    ASSERT_EQ(partition_entry.slices, accumulated_slice_count_per_partition);
    expected_partition_it++;
  }

  for (uint64_t unallocated_partition_index = allocated_partitions + 1;
       unallocated_partition_index < metadata.GetHeader().GetPartitionTableEntryCount();
       ++unallocated_partition_index) {
    const auto& partition_entry = metadata.GetPartitionEntry(unallocated_partition_index);
    EXPECT_FALSE(partition_entry.IsAllocated());
  }

  for (uint64_t unallocated_slice = current_physical_slice;
       unallocated_slice <= metadata.GetHeader().pslice_count; ++unallocated_slice) {
    const auto& slice_entry = metadata.GetSliceEntry(unallocated_slice);
    EXPECT_FALSE(slice_entry.IsAllocated());
  }

  // Unfortunately gTest does not allow a ASSERT_NO_FAILURE() macro to check if any expectation
  // failed in a helper function, so if any expectation failed, inject a FATAL failure, which allows
  // ASSERT_NO_FATAL_FAILURE(Check....) calls;
  if (testing::Test::HasFailure()) {
    FAIL() << "Metadata verification failed.";
  }
}

void CheckImageExtentData(const FvmDescriptor& descriptor, fvm::Metadata& metadata,
                          cpp20::span<const uint8_t> fvm_image_data) {
  uint64_t allocated_partitions = descriptor.partitions().size();
  auto expected_partition_it = descriptor.partitions().begin();

  uint64_t slice_size = descriptor.options().slice_size;

  std::vector<uint8_t> expected_slice_buffer;
  expected_slice_buffer.resize(slice_size, 0);

  uint64_t current_physical_slice = 1;

  for (uint64_t partition_index = 1; partition_index <= allocated_partitions; ++partition_index) {
    const auto& expected_partition_descriptor = *expected_partition_it;
    for (const auto& mapping : expected_partition_descriptor.address().mappings) {
      // Note: even though we could write the slices in arbitrary order and map them to the right
      // vslices, doing so would make this harder to test, and require to re-implement the fvm
      // driver for testing. As a simplification, the slices are streamed by partition order and
      // mapping order, which allows for an easier verification.
      uint64_t size = std::max(mapping.count, mapping.size.value_or(0));
      uint64_t allocated_slice_count =
          volume_image::GetBlockCount(mapping.target, size, slice_size);
      uint64_t data_slice_count =
          volume_image::GetBlockCount(mapping.target, mapping.count, slice_size);
      auto expected_slice_data_view = cpp20::span<uint8_t>(expected_slice_buffer);

      for (uint64_t pslice_offset = 0; pslice_offset < data_slice_count; ++pslice_offset) {
        uint64_t remaining_bytes_in_slice = mapping.count < (pslice_offset + 1) * slice_size
                                                ? mapping.count - (pslice_offset * slice_size)
                                                : slice_size;
        expected_slice_data_view = expected_slice_data_view.subspan(0, remaining_bytes_in_slice);
        expected_partition_descriptor.reader()->Read(mapping.source + pslice_offset * slice_size,
                                                     expected_slice_data_view);
        auto actual_slice_data =
            fvm_image_data.subspan(metadata.GetHeader().GetSliceDataOffset(current_physical_slice),
                                   remaining_bytes_in_slice);
        // The mapping from source,count should match that of target,count.
        // target is slice aligned, and we can figure out which slice is, by reading partition and
        // mappings in order.
        ASSERT_EQ(actual_slice_data.size(), expected_slice_data_view.size());
        EXPECT_TRUE(memcmp(actual_slice_data.data(), expected_slice_data_view.data(),
                           actual_slice_data.size()) == 0);
        current_physical_slice++;
      }

      auto fill_value_it = mapping.options.find(EnumAsString(AddressMapOption::kFill));
      std::optional<uint8_t> fill_value = std::nullopt;
      if (fill_value_it != mapping.options.end()) {
        fill_value = static_cast<uint8_t>(fill_value_it->second);
        memset(expected_slice_buffer.data(), fill_value.value(), slice_size);

        // Check the remainder of the last data slice, filled as well.
        if (expected_slice_data_view.size() < slice_size) {
          auto tail_view = cpp20::span<uint8_t>(expected_slice_buffer)
                               .subspan(expected_slice_data_view.size(),
                                        slice_size - expected_slice_data_view.size());
          EXPECT_TRUE(memcmp(tail_view.data(), expected_slice_buffer.data(), tail_view.size()) ==
                      0);
        }
      }

      // Check that any slice required to be filled, was actually filled, otherwise skip those
      // physical slices.
      for (uint64_t pslice = data_slice_count; pslice < allocated_slice_count; ++pslice) {
        // If filling was requested, check that all filled slices, have this value.
        if (fill_value.has_value()) {
          auto actual_slice_data = fvm_image_data.subspan(
              metadata.GetHeader().GetSliceDataOffset(current_physical_slice), slice_size);
          EXPECT_TRUE(memcmp(actual_slice_data.data(), expected_slice_buffer.data(), slice_size) ==
                      0);
        }
        current_physical_slice++;
      }
    }
    expected_partition_it++;
  }

  // Unfortunately gTest does not allow a ASSERT_NO_FAILURE() macro to check if any expectation
  // failed in a helper function, so if any expectation failed, inject a FATAL failure, which allows
  // ASSERT_NO_FATAL_FAILURE(Check....) calls;
  if (testing::Test::HasFailure()) {
    FAIL() << "Extent Data verification failed.";
  }
}

TEST(FvmDescriptorTest, WriteBlockImageWriterErrorIsError) {
  constexpr uint64_t kSliceSize = 4 * fvm::kBlockSize;
  constexpr uint64_t kMaxSize = 400 * (1ull << 20);
  constexpr uint64_t kTargetSize = 200 * (1ull << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  FvmDescriptor::Builder builder;
  builder.SetOptions(options);

  auto descriptor_or = builder.Build();
  ASSERT_TRUE(descriptor_or.is_ok());

  ErrorWriter writer;
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_error());
}

TEST(FvmDescriptorTest, WriteBlockImagePartitionReaderErrorIsError) {
  constexpr uint64_t kSliceSize = 4 * fvm::kBlockSize;
  constexpr uint64_t kMaxSize = 400 * (1ull << 20);
  constexpr uint64_t kTargetSize = 200 * (1ull << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();

  FvmDescriptor::Builder builder;
  auto descriptor_or =
      builder.SetOptions(options)
          .AddPartition(MakePartitionWithNameAndInstanceGuidAndContentProvider(
              "my-partition", guid_1.value(), fvm::kPlaceHolderInstanceGuid, 8192, 80,
              [](auto offset, auto buffer) -> fpromise::result<void, std::string> {
                return fpromise::error("Oops bad reader.");
              }))
          .Build();

  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_error());
}

TEST(FvmDescriptorTest, WriteBlockImageNoPartitionsIsOk) {
  constexpr uint64_t kSliceSize = 4 * fvm::kBlockSize;
  constexpr uint64_t kMaxSize = 400 * (1ull << 20);
  constexpr uint64_t kTargetSize = 200 * (1ull << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  FvmDescriptor::Builder builder;
  builder.SetOptions(options);

  auto descriptor_or = builder.Build();
  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  // Reduce number of reallocs and memmoves.
  writer.data().reserve(2u << 20);
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.is_error();

  // Verify the metadata matches the supplied options and that no partitions are there.
  // There are no partitions so no required slices, and the options will set everything base
  auto header = internal::MakeHeader(options, 0);
  auto metadata_view = cpp20::span<uint8_t>(writer.data())
                           .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
                                    header.GetMetadataAllocatedBytes());
  auto primary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  metadata_view = cpp20::span<uint8_t>(writer.data())
                      .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
                               header.GetMetadataAllocatedBytes());
  auto secondary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  auto metadata = fvm::Metadata::Create(std::move(primary_metadata_buffer),
                                        std::move(secondary_metadata_buffer));
  EXPECT_TRUE(metadata.is_ok()) << metadata.error_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartitionMetadata(descriptor_or.value(), *metadata));
}

TEST(FvmDescriptorTest, WriteBlockImageWithSinglePartitionMultipleExtentsIsOk) {
  constexpr uint64_t kSliceSize = 4 * fvm::kBlockSize;
  constexpr uint64_t kMaxSize = 400 * (1ull << 20);
  constexpr uint64_t kTargetSize = 200 * (1ull << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();

  FvmDescriptor::Builder builder;
  auto descriptor_or = builder.SetOptions(options)
                           .AddPartition(MakePartitionWithNameAndInstanceGuidAndContentProvider(
                               "my-partition", guid_1.value(), fvm::kPlaceHolderInstanceGuid, 8192,
                               80, &GetContents<1>))
                           .Build();

  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  writer.data().reserve(2u << 20);
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.is_error();

  // Verify the metadata matches the supplied options and that no partitions are there.
  // There are no partitions so no required slices, and the options will set everything base
  auto header = internal::MakeHeader(options, 0);
  auto metadata_view = cpp20::span<uint8_t>(writer.data())
                           .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
                                    header.GetMetadataAllocatedBytes());
  auto primary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  metadata_view = cpp20::span<uint8_t>(writer.data())
                      .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
                               header.GetMetadataAllocatedBytes());
  auto secondary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  auto metadata = fvm::Metadata::Create(std::move(primary_metadata_buffer),
                                        std::move(secondary_metadata_buffer));
  EXPECT_TRUE(metadata.is_ok()) << metadata.error_value();
  ASSERT_NO_FATAL_FAILURE(CheckPartitionMetadata(descriptor_or.value(), *metadata));
  ASSERT_NO_FATAL_FAILURE(CheckImageExtentData(descriptor_or.value(), *metadata, writer.data()));
}

TEST(FvmDescriptorTest, WriteBlockImageWithMultiplePartitionsAndExtentsIsOk) {
  constexpr uint64_t kSliceSize = 4 * fvm::kBlockSize;
  constexpr uint64_t kMaxSize = 400 * (1ull << 20);
  constexpr uint64_t kTargetSize = 200 * (1ull << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.max_volume_size = kMaxSize;
  options.target_volume_size = kTargetSize;

  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();

  auto guid_2 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6C"));
  ASSERT_TRUE(guid_2.is_ok()) << guid_2.error();

  auto guid_3 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6D"));
  ASSERT_TRUE(guid_3.is_ok()) << guid_3.error();

  FvmDescriptor::Builder builder;
  auto descriptor_or = builder.SetOptions(options)
                           .AddPartition(MakePartitionWithNameAndInstanceGuidAndContentProvider(
                               "my-partition-1", guid_1.value(), fvm::kPlaceHolderInstanceGuid,
                               8192, 80, &GetContents<1>))
                           .AddPartition(MakePartitionWithNameAndInstanceGuidAndContentProvider(
                               "my-partition-2", guid_2.value(), fvm::kPlaceHolderInstanceGuid,
                               8192, 60, &GetContents<2>))
                           .AddPartition(MakePartitionWithNameAndInstanceGuidAndContentProvider(
                               "my-partition-3", guid_3.value(), fvm::kPlaceHolderInstanceGuid,
                               8192, 120, &GetContents<3>))
                           .Build();
  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  writer.data().reserve(4u << 20);
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.is_error();

  // Verify the metadata matches the supplied options and that no partitions are there.
  // There are no partitions so no required slices, and the options will set everything base
  auto header = internal::MakeHeader(options, 0);
  auto metadata_view = cpp20::span<uint8_t>(writer.data())
                           .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
                                    header.GetMetadataAllocatedBytes());
  auto primary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  metadata_view = cpp20::span<uint8_t>(writer.data())
                      .subspan(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
                               header.GetMetadataAllocatedBytes());
  auto secondary_metadata_buffer = std::make_unique<MetadataBufferView>(metadata_view);

  auto metadata = fvm::Metadata::Create(std::move(primary_metadata_buffer),
                                        std::move(secondary_metadata_buffer));
  EXPECT_TRUE(metadata.is_ok()) << metadata.error_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartitionMetadata(descriptor_or.value(), *metadata));
  ASSERT_NO_FATAL_FAILURE(CheckImageExtentData(descriptor_or.value(), *metadata, writer.data()));
}

// Added due to OOB read uncovered by integration test.
TEST(FvmDescriptorTest, WriteBlockImageOOBRegressionTest) {
  constexpr uint64_t kSliceSize = 32u * (1u << 10);
  constexpr uint64_t kImageSize = 500u * (1u << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.target_volume_size = kImageSize;

  std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic":11602964,
        "instance_guid":"00000000-0000-0000-0000-000000000000",
        "type_guid":"2967380E-134C-4CBB-B6DA-17E7CE1CA45D",
        "name":"blob",
        "block_size":8192,
        "encryption_type":"ENCRYPTION_TYPE_NONE"
      },
      "address": {
        "magic":12526821592682033285,
        "mappings":[
          {
            "source":0,
            "target":0,
            "count":16384,
            "options":{
              "ADDRESS_MAP_OPTION_FILL":0
              }
          },
          {
            "source":16384,
            "target":536870912,
            "count":8192,
            "size":8192,
            "options":{
              "ADDRESS_MAP_OPTION_FILL":0
            }
          },
          {
            "source":24576,
            "target":1073741824,
            "count":655360,
            "size":655360,
            "options":{
              "ADDRESS_MAP_OPTION_FILL":0
              }
            },
            {
              "source":1236992,
              "target":2147483648,
              "count":32768,
              "size":32768
            },
            {
              "source":679936,
              "target":1610612736,
              "count":557056
          }
        ]
      }
    }
    )";

  auto partition_or =
      Partition::Create(kSerializedVolumeImage, std::make_unique<FakeReader>(&GetContents<1>));
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();

  FvmDescriptor::Builder builder;
  auto descriptor_or =
      builder.SetOptions(options).AddPartition(std::move(partition_or.value())).Build();
  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  writer.data().reserve(4u << 20);
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.is_error();
}

// Added due Off By one in certain configurations on unfilled mappings.
TEST(FvmDescriptorTest, WriteBlockImagesOffByOneRegressionTest) {
  constexpr uint64_t kSliceSize = 32u * (1u << 10);
  constexpr uint64_t kImageSize = 500u * (1u << 20);

  auto options = ValidOptions();
  options.slice_size = kSliceSize;
  options.target_volume_size = kImageSize;

  std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic":11602964,
        "instance_guid":"00000000-0000-0000-0000-000000000000",
        "type_guid":"2967380E-134C-4CBB-B6DA-17E7CE1CA45D",
        "name":"blob",
        "block_size":8192,
        "encryption_type":"ENCRYPTION_TYPE_NONE"
      },
      "address": {
        "magic":12526821592682033285,
        "mappings":[
          {
            "source":0,
            "target":0,
            "count":16384,
            "options":{
              "ADDRESS_MAP_OPTION_FILL":0
              }
          },
          {
            "source":16384,
            "target":536870912,
            "count":8192,
            "size":98304
          }
        ]
      }
    }
    )";

  auto partition_or =
      Partition::Create(kSerializedVolumeImage, std::make_unique<FakeReader>(&GetContents<1>));
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();

  FvmDescriptor::Builder builder;
  auto descriptor_or =
      builder.SetOptions(options).AddPartition(std::move(partition_or.value())).Build();
  ASSERT_TRUE(descriptor_or.is_ok());

  FakeWriter writer;
  writer.data().reserve(4u << 20);
  auto write_result = descriptor_or.value().WriteBlockImage(writer);
  ASSERT_TRUE(write_result.is_ok()) << write_result.is_error();
}

}  // namespace
}  // namespace storage::volume_image
