// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <fvm/format.h>

#include <zxtest/zxtest.h>

namespace fvm {
namespace {

constexpr uint8_t kZeroType[sizeof(VPartitionEntry::type)] = {0};
constexpr uint8_t kZeroGuid[sizeof(VPartitionEntry::guid)] = {0};
constexpr uint8_t kZeroName[sizeof(VPartitionEntry::name)] = {0};

TEST(VPartitionEntryTest, DefaultsToUnallocatedAndZeroed) {
    VPartitionEntry entry = VPartitionEntry::Create();

    ASSERT_EQ(entry.slices, 0);
    ASSERT_EQ(entry.flags, 0);
    ASSERT_BYTES_EQ(entry.type, kZeroType, sizeof(kZeroType));
    ASSERT_BYTES_EQ(entry.guid, kZeroGuid, sizeof(kZeroGuid));
    ASSERT_BYTES_EQ(entry.name, kZeroName, sizeof(kZeroName));
    ASSERT_FALSE(entry.IsAllocated());
    ASSERT_TRUE(entry.IsFree());
    ASSERT_TRUE(entry.IsActive());
    ASSERT_FALSE(entry.IsInactive());
}

TEST(VPartitionEntryTest, CreateValuesAreOkAndFlagsAreFiltered) {
    static constexpr uint8_t kType[sizeof(VPartitionEntry::type)] = {1, 2, 3, 4};
    static constexpr uint8_t kGuid[sizeof(VPartitionEntry::guid)] = {4, 3, 2, 1};
    static constexpr uint8_t kName[sizeof(VPartitionEntry::name)] = {'a', 'b', 'c', '\0'};
    // Set all bits.
    constexpr uint32_t kFlags = ~0;
    constexpr uint32_t kSlices = 20;

    VPartitionEntry entry = VPartitionEntry::Create(kType, kGuid, kSlices,
                                                    reinterpret_cast<const char*>(kName), kFlags);

    ASSERT_EQ(entry.slices, kSlices);
    // Verify that only the parsed flags are propagated into the entry data.
    EXPECT_EQ(entry.flags, VPartitionEntry::ParseFlags(kFlags));
    EXPECT_BYTES_EQ(entry.type, kType, sizeof(kType));
    EXPECT_BYTES_EQ(entry.guid, kGuid, sizeof(kGuid));
    EXPECT_BYTES_EQ(entry.name, kName, sizeof(kName));
    EXPECT_TRUE(entry.IsAllocated());
    EXPECT_FALSE(entry.IsFree());
    EXPECT_FALSE(entry.IsActive());
    EXPECT_TRUE(entry.IsInactive());
}

TEST(VPartitionEntryTest, SetActiveModifiesActiveView) {
    VPartitionEntry entry = VPartitionEntry::Create();

    ASSERT_TRUE(entry.IsActive());
    entry.SetActive(false);
    ASSERT_FALSE(entry.IsActive());
    ASSERT_TRUE(entry.IsInactive());

    entry.SetActive(true);
    ASSERT_TRUE(entry.IsActive());
    ASSERT_FALSE(entry.IsInactive());
}

TEST(VPartitionEntryTest, UpdatingSliceCountIsAllocated) {
    VPartitionEntry entry = VPartitionEntry::Create();

    ASSERT_FALSE(entry.IsAllocated());
    ASSERT_TRUE(entry.IsFree());
    entry.slices++;

    ASSERT_TRUE(entry.IsAllocated());
    ASSERT_FALSE(entry.IsFree());
}

TEST(VPartitionEntryTest, ReleaseZeroesAndMarksAsFree) {
    VPartitionEntry entry = VPartitionEntry::Create();
    entry.slices++;

    ASSERT_TRUE(entry.IsAllocated());
    ASSERT_FALSE(entry.IsFree());

    entry.Release();
    ASSERT_EQ(entry.slices, 0);
    ASSERT_EQ(entry.flags, 0);
    ASSERT_BYTES_EQ(entry.type, kZeroType, sizeof(kZeroType));
    ASSERT_BYTES_EQ(entry.guid, kZeroGuid, sizeof(kZeroGuid));
    ASSERT_BYTES_EQ(entry.name, kZeroName, sizeof(kZeroGuid));
    ASSERT_FALSE(entry.IsAllocated());
    ASSERT_TRUE(entry.IsFree());
    ASSERT_TRUE(entry.IsActive());
    ASSERT_FALSE(entry.IsInactive());
}

} // namespace
} // namespace fvm
