// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/partition.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/volume_image/fvm/address_descriptor.h"
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

}  // namespace
}  // namespace storage::volume_image
