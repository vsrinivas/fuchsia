// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/empty_partition.h"

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"

namespace storage::volume_image {
namespace {

TEST(EmptyPartitionTest, MissingOrZeroMaxBytesIsError) {
  PartitionOptions partition_options;
  FvmOptions fvm_options;
  fvm_options.slice_size = 8192;

  ASSERT_TRUE(CreateEmptyFvmPartition(partition_options, fvm_options).is_error());

  partition_options.max_bytes = 0;
  ASSERT_TRUE(CreateEmptyFvmPartition(partition_options, fvm_options).is_error());
}

TEST(EmptyPartitionTest, ZeroSliceSizeIsError) {
  PartitionOptions partition_options;
  partition_options.max_bytes = 1;
  FvmOptions fvm_options;
  fvm_options.slice_size = 0;

  ASSERT_TRUE(CreateEmptyFvmPartition(partition_options, fvm_options).is_error());
}

TEST(EmptyPartitionTest, AllocatesSlicesToStoreMaxBytes) {
  PartitionOptions partition_options;
  partition_options.max_bytes = 8193;
  FvmOptions fvm_options;
  fvm_options.slice_size = 8192;

  auto res = CreateEmptyFvmPartition(partition_options, fvm_options);
  ASSERT_TRUE(res.is_ok());

  auto partition = res.take_value();

  EXPECT_TRUE(partition.volume().name.empty());

  ASSERT_EQ(partition.address().mappings.size(), 1u);

  const auto& mapping = partition.address().mappings.front();
  EXPECT_EQ(mapping.source, 0u);
  EXPECT_EQ(mapping.target, 0u);
  EXPECT_EQ(mapping.count, 0u);
  EXPECT_EQ(mapping.size, partition_options.max_bytes.value());
  EXPECT_EQ(mapping.options.at(EnumAsString(AddressMapOption::kFill)), 0u);
  EXPECT_TRUE(memcmp(partition.volume().instance.data(), fvm::kPlaceHolderInstanceGuid.data(),
                     fvm::kPlaceHolderInstanceGuid.size()) == 0);
}

}  // namespace
}  // namespace storage::volume_image
