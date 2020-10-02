// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>

#include <vector>

#include <fvm/format.h>
#include <fvm/test/device-ref.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// Shared constants for all resize tests.
constexpr uint64_t kTestBlockSize = 512;
constexpr uint64_t kSliceSize = 1 << 20;

constexpr uint64_t kDataSizeInBlocks = 10;
constexpr uint64_t kDataSize = kTestBlockSize * kDataSizeInBlocks;

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

void GrowFvm(const fbl::unique_fd& devfs_root, const GrowParams& params, RamdiskRef* ramdisk,
             FvmAdapter* fvm_adapter) {
  std::unique_ptr<VPartitionAdapter> vpartition = nullptr;
  ASSERT_OK(fvm_adapter->AddPartition(devfs_root, kPartitionName, Guid(kPartitionUniqueGuid),
                                      Guid(kPartitionTypeGuid), kPartitionSliceCount, &vpartition),
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

class FvmResizeTest : public zxtest::Test {
 public:
  void SetUp() override {
    devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";
    ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr_));
  }

 protected:
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

TEST_F(FvmResizeTest, PreallocatedMetadataGrowsCorrectly) {
  constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kTestBlockSize;
  constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kTestBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount);
  ASSERT_TRUE(ramdisk);
  std::unique_ptr<FvmAdapter> fvm =
      FvmAdapter::CreateGrowable(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount,
                                 kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  GrowParams params;
  params.target_size = kMaxBlockCount * kTestBlockSize;
  // Data stays the same size, so there are no new slices.
  params.validate_new_slices = true;
  params.format = FormatInfo::FromDiskSize(kMaxBlockCount * kTestBlockSize, kSliceSize);
  params.seed = zxtest::Runner::GetInstance()->options().seed;

  ASSERT_NO_FATAL_FAILURES(GrowFvm(devmgr_.devfs_root(), params, ramdisk.get(), fvm.get()));
}

TEST_F(FvmResizeTest, PreallocatedMetadataGrowsAsMuchAsPossible) {
  constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kTestBlockSize;
  constexpr uint64_t kMaxBlockCount = (4 << 10) * kSliceSize / kTestBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount);
  ASSERT_TRUE(ramdisk);
  std::unique_ptr<FvmAdapter> fvm =
      FvmAdapter::CreateGrowable(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount,
                                 kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  // Compute the expected header information. This is the header computed for the original slice
  // size, expanded by as many slices as possible.
  Header expected =
      Header::FromDiskSize(kMaxUsablePartitions, kMaxBlockCount * kTestBlockSize, kSliceSize);
  expected.SetSliceCount(expected.GetAllocationTableAllocatedEntryCount());

  GrowParams params;
  // This defines a target size much larger than our header could handle so the resize will max
  // out the slices in the headeer.
  params.target_size = 2 * expected.fvm_partition_size;
  // Data stays the same size, so there are no new slices.
  params.validate_new_slices = false;
  params.format = FormatInfo(expected);
  params.seed = zxtest::Runner::GetInstance()->options().seed;

  ASSERT_NO_FATAL_FAILURES(GrowFvm(devmgr_.devfs_root(), params, ramdisk.get(), fvm.get()));
}

TEST_F(FvmResizeTest, PreallocatedMetadataRemainsValidInPartialGrowths) {
  constexpr uint64_t kInitialBlockCount = (50 * kSliceSize) / kTestBlockSize;
  constexpr uint64_t kMidBlockCount = (4 << 10) * kSliceSize / kTestBlockSize;
  constexpr uint64_t kMaxBlockCount = (8 << 10) * kSliceSize / kTestBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount);
  ASSERT_TRUE(ramdisk);
  std::unique_ptr<FvmAdapter> fvm =
      FvmAdapter::CreateGrowable(devmgr_.devfs_root(), kTestBlockSize, kInitialBlockCount,
                                 kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  GrowParams params;
  params.target_size = kMidBlockCount * kTestBlockSize;
  // Data stays the same size, so there are no new slices.
  params.validate_new_slices = true;
  params.format = FormatInfo::FromPreallocatedSize(kMidBlockCount * kTestBlockSize,
                                                   kMaxBlockCount * kTestBlockSize, kSliceSize);
  params.seed = zxtest::Runner::GetInstance()->options().seed;

  ASSERT_NO_FATAL_FAILURES(GrowFvm(devmgr_.devfs_root(), params, ramdisk.get(), fvm.get()));

  params.format = FormatInfo::FromPreallocatedSize(kMaxBlockCount * kTestBlockSize,
                                                   kMaxBlockCount * kTestBlockSize, kSliceSize);
  params.target_size = kMaxBlockCount * kTestBlockSize;
  ASSERT_NO_FATAL_FAILURES(GrowFvm(devmgr_.devfs_root(), params, ramdisk.get(), fvm.get()));
}

}  // namespace
}  // namespace fvm
