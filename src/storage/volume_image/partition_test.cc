// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/partition.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {
namespace {

Partition MakePartitionWithNameAndInstanceGuid(
    std::string_view name, const std::array<uint8_t, kGuidLength>& instance_guid) {
  VolumeDescriptor volume = {};
  volume.name = name;
  volume.instance = instance_guid;
  AddressDescriptor address = {};
  return Partition(volume, address, nullptr);
}

struct PartitionInfo {
  std::string name = {};
  std::array<uint8_t, kGuidLength> instance_guid = {};
};

TEST(PartitionLessThanTest, WithDifferentNameOrdersLexicographicallyByName) {
  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  auto guid_2 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6A"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();
  ASSERT_TRUE(guid_2.is_ok()) << guid_2.error();
  Partition first = MakePartitionWithNameAndInstanceGuid("partition-name", guid_1.value());
  Partition second = MakePartitionWithNameAndInstanceGuid("partition-name-a", guid_2.value());
  ASSERT_NE(first.volume().name, second.volume().name);
  ASSERT_TRUE(first.volume().name < second.volume().name);

  Partition::LessThan is_before;
  EXPECT_TRUE(is_before(first, second));
  EXPECT_FALSE(is_before(first, first));
  EXPECT_FALSE(is_before(second, first));
}

TEST(PartitionLessThanTest, WithSameNameOrdersLexicographicallyByInstanceGuid) {
  auto guid_1 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6A"));
  ASSERT_TRUE(guid_1.is_ok()) << guid_1.error();
  auto guid_2 = Guid::FromString(std::string_view("08185F0C-892D-428A-A789-DBEEC8F55E6B"));
  ASSERT_TRUE(guid_2.is_ok()) << guid_2.error();

  Partition first = MakePartitionWithNameAndInstanceGuid("partition-name", guid_1.value());
  Partition second = MakePartitionWithNameAndInstanceGuid("partition-name", guid_2.value());

  ASSERT_EQ(first.volume().name, second.volume().name);

  ASSERT_TRUE(std::lexicographical_compare(
      first.volume().instance.cbegin(), first.volume().instance.cend(),
      second.volume().instance.cbegin(), second.volume().instance.cend()));
  Partition::LessThan is_before;
  EXPECT_TRUE(is_before(first, second));
  EXPECT_FALSE(is_before(first, first));
  EXPECT_FALSE(is_before(second, first));
}

// Placeholder reader.
class FakeReader final : public Reader {
 public:
  ~FakeReader() final = default;

  uint64_t GetMaximumOffset() const override { return 0; }

  fit::result<void, std::string> Read(uint64_t offset, fbl::Span<uint8_t> buffer) const final {
    return fit::ok();
  }
};

TEST(PartitionTest, CreateFromValidVolumeImageIsOk) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic": 11602964,
        "instance_guid": "04030201-0605-0807-1009-111213141516",
        "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5",
        "name": "partition-1",
        "block_size": 512,
        "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
        "options" : [
          "OPTION_NONE",
          "OPTION_EMPTY"
        ]
      },
      "address": {
          "magic": 12526821592682033285,
          "mappings": [
            {
              "source": 20,
              "target": 400,
              "count": 10
            }
          ]
      }
    })";

  std::unique_ptr<Reader> fake_reader(new FakeReader());
  auto* expected_reader = fake_reader.get();
  auto result = Partition::Create(kSerializedVolumeImage, std::move(fake_reader));
  ASSERT_TRUE(result.is_ok());
  auto partition = result.take_value();

  // Sanity check that values are actually set.
  EXPECT_EQ(expected_reader, partition.reader());
  EXPECT_STREQ("partition-1", partition.volume().name.data());
  EXPECT_EQ(1u, partition.address().mappings.size());
}

TEST(PartitionTest, CreateFromInvalidJsonIsError) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
     )";

  auto result = Partition::Create(kSerializedVolumeImage, nullptr);
  ASSERT_FALSE(result.is_ok());
}

TEST(PartitionTest, CreateFromValidJsonWithMissingVolumeIsError) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
      "address": {
          "magic": 12526821592682033285,
          "mappings": [
            {
              "source": 20,
              "target": 400,
              "count": 10
            }
          ]
      }
    })";

  auto result = Partition::Create(kSerializedVolumeImage, nullptr);
  ASSERT_FALSE(result.is_ok());
}

TEST(PartitionTest, CreateFromValidJsonWithMissingAddressIsError) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic": 11602964,
        "instance_guid": "04030201-0605-0807-1009-111213141516",
        "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5",
        "name": "partition-1",
        "block_size": 512,
        "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
        "options" : [
          "OPTION_NONE",
          "OPTION_EMPTY"
        ]
      }
    })";

  auto result = Partition::Create(kSerializedVolumeImage, nullptr);
  ASSERT_FALSE(result.is_ok());
}

TEST(PartitionTest, CreateFromVolumeImageWithInvalidVolumeIsError) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic": 0,
      },
      "address": {
          "magic": 12526821592682033285,
          "mappings": [
            {
              "source": 20,
              "target": 400,
              "count": 10
            }
          ]
      }
    })";

  auto result = Partition::Create(kSerializedVolumeImage, nullptr);
  ASSERT_FALSE(result.is_ok());
}

TEST(PartitionTest, CreateFromVolumeImageWithInvalidAddressIsError) {
  constexpr std::string_view kSerializedVolumeImage = R"(
    {
      "volume": {
        "magic": 11602964,
        "instance_guid": "04030201-0605-0807-1009-111213141516",
        "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5",
        "name": "partition-1",
        "block_size": 512,
        "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
        "options" : [
          "OPTION_NONE",
          "OPTION_EMPTY"
        ]
      },
      "address": {
          "magic": 0,
      }
    })";

  auto result = Partition::Create(kSerializedVolumeImage, nullptr);
  ASSERT_FALSE(result.is_ok());
}

}  // namespace
}  // namespace storage::volume_image
