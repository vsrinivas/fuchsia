// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

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

struct GrowParams {
    // random seed.
    unsigned int seed;

    // Target size of the ramdisk.
    int64_t target_size;

    // The expected format info at each step.
    FormatInfo format;

    // Attempt to allocate, read and write to new slices.
    bool validate_new_slices;
};

void GrowFvm(const GrowParams& params, RamdiskRef* ramdisk, FvmAdapter* fvm_adapter) {
    std::unique_ptr<VPartitionAdapter> vpartition = nullptr;
    ASSERT_OK(fvm_adapter->AddPartition(kPartitionName, Guid(kPartitionUniqueGuid),
                                        Guid(kPartitionTypeGuid), kPartitionSliceCount,
                                        &vpartition),
              "Failed to add partition.");
    ASSERT_TRUE(vpartition);

    // Get current state of the FVM.
    VolumeInfo before_grow_info;
    ASSERT_OK(fvm_adapter->Query(&before_grow_info));
    ASSERT_EQ(kSliceSize, before_grow_info.slice_size);
    ASSERT_EQ(kPartitionSliceCount, before_grow_info.pslice_allocated_count);

    unsigned int initial_seed = params.seed;
    auto random_data = MakeRandomBuffer(kDataSize, &initial_seed);
    ASSERT_NO_FATAL_FAILURES(vpartition->WriteAt(random_data, 0));

    // Grow the Device.
    ASSERT_OK(ramdisk->Grow(params.target_size));

    // Rebind FVM and get a new connection to the vpartitions when they become available.
    ASSERT_OK(fvm_adapter->Rebind({vpartition.get()}));

    // Get stats after growth.
    VolumeInfo after_grow_info;
    ASSERT_OK(fvm_adapter->Query(&after_grow_info));
    ASSERT_TRUE(IsConsistentAfterGrowth(before_grow_info, after_grow_info));
    ASSERT_EQ(params.format.slice_count(), after_grow_info.pslice_total_count);
    // Data should still be present.
    ASSERT_NO_FATAL_FAILURES(vpartition->CheckContentsAt(random_data, 0));

    // Verify new slices can be allocated, written to and read from.
    if (params.validate_new_slices) {
        ASSERT_OK(vpartition->Extend(kPartitionSliceCount,
                                     after_grow_info.pslice_total_count - kPartitionSliceCount));

        auto random_data_2 = MakeRandomBuffer(kDataSize, &initial_seed);
        uint64_t offset = (params.format.slice_count() - 1) * kSliceSize;
        ASSERT_NO_FATAL_FAILURES(vpartition->WriteAt(random_data_2, offset));
        ASSERT_NO_FATAL_FAILURES(vpartition->CheckContentsAt(random_data_2, offset));
    }
}

TEST(FvmResizeTest, NonPreallocatedMetadataIsUnaffected) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kBlockSize;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm =
        FvmAdapter::Create(kBlockSize, kInitialBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);

    GrowParams params;
    params.target_size = kMaxBlockCount * kBlockSize;
    // Data stays the same size, so there are no new slices.
    params.validate_new_slices = false;
    params.format = FormatInfo::FromDiskSize(kInitialBlockCount * kBlockSize, kSliceSize);
    params.seed = zxtest::Runner::GetInstance()->options().seed;

    ASSERT_NO_FATAL_FAILURES(GrowFvm(params, ramdisk.get(), fvm.get()));
}

TEST(FvmResizeTest, PreallocatedMetadataGrowsCorrectly) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kBlockSize;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
        kBlockSize, kInitialBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);

    GrowParams params;
    params.target_size = kMaxBlockCount * kBlockSize;
    // Data stays the same size, so there are no new slices.
    params.validate_new_slices = true;
    params.format = FormatInfo::FromDiskSize(kMaxBlockCount * kBlockSize, kSliceSize);
    params.seed = zxtest::Runner::GetInstance()->options().seed;

    ASSERT_NO_FATAL_FAILURES(GrowFvm(params, ramdisk.get(), fvm.get()));
}

TEST(FvmResizeTest, PreallocatedMetadataGrowsAsMuchAsPossible) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kBlockSize;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
        kBlockSize, kInitialBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);

    GrowParams params;
    params.target_size = 2 * kMaxBlockCount * kBlockSize;
    // Data stays the same size, so there are no new slices.
    params.validate_new_slices = false;
    params.format = FormatInfo::FromDiskSize(kMaxBlockCount * kBlockSize, kSliceSize);
    params.seed = zxtest::Runner::GetInstance()->options().seed;

    ASSERT_NO_FATAL_FAILURES(GrowFvm(params, ramdisk.get(), fvm.get()));
}

TEST(FvmResizeTest, PreallocatedMetadataRemainsValidInPartialGrowths) {
    constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kBlockSize;
    constexpr uint64_t kMidBlockCount = (4 << 10) * kSliceSize / kBlockSize;
    constexpr uint64_t kMaxBlockCount = (8 << 10) * kSliceSize / kBlockSize;

    std::unique_ptr<RamdiskRef> ramdisk = RamdiskRef::Create(kBlockSize, kInitialBlockCount);
    ASSERT_TRUE(ramdisk);
    std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
        kBlockSize, kInitialBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
    ASSERT_TRUE(fvm);

    GrowParams params;
    params.target_size = kMidBlockCount * kBlockSize;
    // Data stays the same size, so there are no new slices.
    params.validate_new_slices = true;
    params.format = FormatInfo::FromPreallocatedSize(kMidBlockCount * kBlockSize,
                                                     kMaxBlockCount * kBlockSize, kSliceSize);
    params.seed = zxtest::Runner::GetInstance()->options().seed;

    ASSERT_NO_FATAL_FAILURES(GrowFvm(params, ramdisk.get(), fvm.get()));

    params.format = FormatInfo::FromDiskSize(kMaxBlockCount * kBlockSize, kSliceSize);
    params.target_size = kMaxBlockCount * kBlockSize;
    ASSERT_NO_FATAL_FAILURES(GrowFvm(params, ramdisk.get(), fvm.get()));
}

} // namespace
} // namespace fvm
