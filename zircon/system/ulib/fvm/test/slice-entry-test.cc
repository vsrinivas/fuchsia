// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

constexpr uint64_t kVPartition = 15;
constexpr uint64_t kVSlice = 25;

TEST(SliceEntryTest, DefaultsToUnallocatedAndZeroed) {
  SliceEntry entry;
  ASSERT_FALSE(entry.IsAllocated());
  ASSERT_TRUE(entry.IsFree());
  ASSERT_EQ(entry.VPartition(), 0);
  ASSERT_EQ(entry.VSlice(), 0);
}

TEST(SliceEntryTest, CreateAllocatesAndSetsVSliceAndVPartition) {
  SliceEntry entry(kVPartition, kVSlice);
  EXPECT_TRUE(entry.IsAllocated());
  EXPECT_FALSE(entry.IsFree());
  EXPECT_EQ(entry.VPartition(), kVPartition);
  EXPECT_EQ(entry.VSlice(), kVSlice);
}

TEST(SliceEntryTest, SetAllocatesAndSetsVSliceAndVPartition) {
  SliceEntry entry;
  ASSERT_TRUE(entry.IsFree());
  ASSERT_EQ(entry.VPartition(), 0);
  ASSERT_EQ(entry.VSlice(), 0);

  entry.Set(kVPartition, kVSlice);

  EXPECT_TRUE(entry.IsAllocated());
  EXPECT_FALSE(entry.IsFree());
  EXPECT_EQ(entry.VPartition(), kVPartition);
  EXPECT_EQ(entry.VSlice(), kVSlice);
}

TEST(SliceEntryTest, ReleaseZeroesAndDeallocates) {
  SliceEntry entry(kVPartition, kVSlice);

  entry.Release();

  ASSERT_FALSE(entry.IsAllocated());
  ASSERT_TRUE(entry.IsFree());
  ASSERT_EQ(entry.VPartition(), 0);
  ASSERT_EQ(entry.VSlice(), 0);
}

}  // namespace
}  // namespace fvm
