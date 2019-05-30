// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace fvm {
namespace {

// Shared constants for all resize tests.
constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kSliceSize = 1 << 20;

constexpr uint64_t kDataSizeInBlocks = 10;
constexpr uint64_t kDataSize = kBlockSize * kDataSizeInBlocks;

constexpr char kPartitionName[] = "partition-name";
constexpr uint8_t kPartitionUniqueGuid[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr uint8_t kPartitionTypeGuid[] = {0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99,
                                          0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
constexpr uint64_t kPartitionSliceCount = 1;

TEST(FvmResizeTest, NonPreallocatedMetadataIsUnaffected) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kBlockSize;

    unsigned int initial_seed = zxtest::Runner::GetInstance()->options().seed;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm =
        FvmAdapter::Create(kBlockSize, kInitialBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);
    std::unique_ptr<VPartitionAdapter> vpartition = nullptr;
    ASSERT_OK(fvm->AddPartition(kPartitionName, Guid(kPartitionUniqueGuid),
                                Guid(kPartitionTypeGuid), kPartitionSliceCount, &vpartition),
              "Failed to add partition.");
    ASSERT_TRUE(vpartition);

    // Get current state of the FVM.
    VolumeInfo before_grow_info;
    ASSERT_OK(fvm->Query(&before_grow_info));
    ASSERT_EQ(kSliceSize, before_grow_info.slice_size);
    ASSERT_EQ(kPartitionSliceCount, before_grow_info.pslice_allocated_count);

    auto random_data = MakeRandomBuffer(kDataSize, &initial_seed);
    ASSERT_NO_FATAL_FAILURES(vpartition->WriteAt(random_data, 0));

    // Grow the Device.
    ASSERT_OK(ramdisk->Grow(kBlockSize * kMaxBlockCount));

    // Rebind FVM and get a new connection to the vpartitions when they become available.
    ASSERT_OK(fvm->Rebind({vpartition.get()}));

    // Get stats after growth.
    VolumeInfo after_grow_info;
    ASSERT_OK(fvm->Query(&after_grow_info));
    ASSERT_TRUE(IsConsistentAfterGrowth(before_grow_info, after_grow_info));
    ASSERT_BYTES_EQ(&before_grow_info, &after_grow_info, sizeof(VolumeInfo));
    // Data should still be present.
    ASSERT_NO_FATAL_FAILURES(vpartition->CheckContentsAt(random_data, 0));
}

TEST(FvmResizeTest, PreallocatedMetadataGrowsCorrectly) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kBlockSize;

    unsigned int initial_seed = zxtest::Runner::GetInstance()->options().seed;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(kBlockSize, kInitialBlockCount,
                                                                 kMaxBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);
    std::unique_ptr<VPartitionAdapter> vpartition = nullptr;
    ASSERT_OK(fvm->AddPartition(kPartitionName, static_cast<Guid>(kPartitionUniqueGuid),
                                static_cast<Guid>(kPartitionTypeGuid), kPartitionSliceCount,
                                &vpartition),
              "Failed to add partition.");
    ASSERT_TRUE(vpartition);

    // Expected metadata info.
    FormatInfo final_format_info =
        FormatInfo::FromDiskSize(kMaxBlockCount * kBlockSize, kSliceSize);

    // Get current state of the FVM.
    VolumeInfo before_grow_info;
    ASSERT_OK(fvm->Query(&before_grow_info));
    ASSERT_EQ(kSliceSize, before_grow_info.slice_size);
    ASSERT_EQ(kPartitionSliceCount, before_grow_info.pslice_allocated_count);

    auto random_data = MakeRandomBuffer(kDataSize, &initial_seed);
    ASSERT_NO_FATAL_FAILURES(vpartition->WriteAt(random_data, 0));

    // Grow the Device.
    ASSERT_OK(ramdisk->Grow(kBlockSize * kMaxBlockCount));

    // Rebind FVM and get a new connection to the vpartitions when they become available.
    ASSERT_OK(fvm->Rebind({vpartition.get()}));

    // Get stats after growth.
    VolumeInfo after_grow_info;
    ASSERT_OK(fvm->Query(&after_grow_info));
    ASSERT_TRUE(IsConsistentAfterGrowth(before_grow_info, after_grow_info));
    // Check that new slices are available
    ASSERT_EQ(final_format_info.slice_count(), after_grow_info.pslice_total_count);

    // Data should still be persisted.
    ASSERT_NO_FATAL_FAILURES(vpartition->CheckContentsAt(random_data, 0));

    // Verify new slices can be allocated, written to and read from.
    ASSERT_OK(vpartition->Extend(kPartitionSliceCount,
                                 after_grow_info.pslice_total_count - kPartitionSliceCount));

    auto random_data_2 = MakeRandomBuffer(kDataSize, &initial_seed);
    uint64_t offset = (final_format_info.slice_count() - 1) * kSliceSize;
    ASSERT_NO_FATAL_FAILURES(vpartition->WriteAt(random_data_2, offset));
    ASSERT_NO_FATAL_FAILURES(vpartition->CheckContentsAt(random_data_2, offset));
}

} // namespace
} // namespace fvm
