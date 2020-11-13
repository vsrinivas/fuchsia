// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include <fvm/format.h>
#include <fvm/test/device-ref.h>
#include <zxtest/zxtest.h>

namespace fvm {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kSliceSize = 1 << 20;

// using Partition = llcpp::fuchsia::hardware::block::partition::Partition;
using Volume = ::llcpp::fuchsia::hardware::block::volume::Volume;
using VolumeManager = ::llcpp::fuchsia::hardware::block::volume::VolumeManager;

class FvmVolumeManagerApiTest : public zxtest::Test {
 public:
  static void SetUpTestCase() {
    devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");

    devmgr_ = std::make_unique<devmgr_integration_test::IsolatedDevmgr>();
    ASSERT_OK(devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), devmgr_.get()));
  }

  static void TearDownTestCase() { devmgr_.reset(); }

 protected:
  static std::unique_ptr<devmgr_integration_test::IsolatedDevmgr> devmgr_;
};

std::unique_ptr<devmgr_integration_test::IsolatedDevmgr> FvmVolumeManagerApiTest::devmgr_ = nullptr;

TEST_F(FvmVolumeManagerApiTest, GetInfoNonPreallocatedMetadata) {
  constexpr uint64_t kBlockCount = (50 * kSliceSize) / kBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount);
  ASSERT_TRUE(ramdisk);

  std::unique_ptr<FvmAdapter> fvm =
      FvmAdapter::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  const fvm::Header kExpectedFormat =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, kBlockSize * kBlockCount, kSliceSize);

  VolumeManager::ResultOf::GetInfo result = VolumeManager::Call::GetInfo(fvm->device()->channel());

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result->status, "Service returned error.");

  // Check API returns the correct information for a non preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size, result->info->slice_size);
  // Less or equal, because the metadata size is rounded to the nearest block boundary.
  EXPECT_LE(result->info->current_slice_count, result->info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.GetMaxAllocationTableEntriesForDiskSize(kBlockSize * kBlockCount),
            result->info->current_slice_count);
  EXPECT_EQ(kExpectedFormat.GetAllocationTableAllocatedEntryCount(),
            result->info->maximum_slice_count);
}

TEST_F(FvmVolumeManagerApiTest, GetInfoWithPreallocatedMetadata) {
  constexpr uint64_t kBlockCount = (50 * kSliceSize) / kBlockSize;
  constexpr uint64_t kMaxBlockCount = 1024 * kSliceSize / kBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount);
  ASSERT_TRUE(ramdisk);

  std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
      devmgr_->devfs_root(), kBlockSize, kBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  const fvm::Header kExpectedFormat = fvm::Header::FromGrowableDiskSize(
      fvm::kMaxUsablePartitions, kBlockSize * kBlockCount, kBlockSize * kMaxBlockCount, kSliceSize);

  VolumeManager::ResultOf::GetInfo result = VolumeManager::Call::GetInfo(fvm->device()->channel());

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result->status, "Service returned error.");

  // Check API returns the correct information for a preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size, result->info->slice_size);
  // Less than because we picked sizes that enforce a difference.
  EXPECT_LT(result->info->current_slice_count, result->info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.pslice_count, result->info->current_slice_count);
  EXPECT_EQ(kExpectedFormat.GetAllocationTableAllocatedEntryCount(),
            result->info->maximum_slice_count);
}

// Tests that the maximum extents apply to partition growth properly.
TEST_F(FvmVolumeManagerApiTest, PartitionLimit) {
  constexpr uint64_t kBlockCount = (50 * kSliceSize) / kBlockSize;
  constexpr uint64_t kMaxBlockCount = 1024 * kSliceSize / kBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount);
  ASSERT_TRUE(ramdisk);

  std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
      devmgr_->devfs_root(), kBlockSize, kBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  // Type GUID for partition.
  llcpp::fuchsia::hardware::block::partition::GUID type_guid;
  std::fill(std::begin(type_guid.value), std::end(type_guid.value), 0x11);

  // Instance GUID for partition.
  llcpp::fuchsia::hardware::block::partition::GUID guid;
  std::fill(std::begin(guid.value), std::end(guid.value), 0x12);

  // The partition hasn't been created yet, the result should be "not found".
  VolumeManager::ResultOf::GetPartitionLimit unfound_result =
      VolumeManager::Call::GetPartitionLimit(fvm->device()->channel(), guid);
  ASSERT_OK(unfound_result.status(), "Transport layer error");
  ASSERT_EQ(unfound_result->status, ZX_ERR_NOT_FOUND);

  // Create the partition inside FVM with one slice.
  const char kPartitionName[] = "mypart";
  VolumeManager::ResultOf::AllocatePartition alloc_result = VolumeManager::Call::AllocatePartition(
      fvm->device()->channel(), 1, type_guid, guid, kPartitionName, 0);
  ASSERT_OK(alloc_result.status(), "Transport layer error");
  ASSERT_OK(alloc_result->status, "Service returned error.");

  // That partition's initial limit should be 0 (no limit).
  VolumeManager::ResultOf::GetPartitionLimit get_result =
      VolumeManager::Call::GetPartitionLimit(fvm->device()->channel(), guid);
  ASSERT_OK(get_result.status(), "Transport layer error");
  ASSERT_OK(get_result->status, "Service returned error.");
  EXPECT_EQ(get_result->byte_count, 0, "Expected 0 limit on init.");

  // Set the limit to two slices.
  VolumeManager::ResultOf::SetPartitionLimit set_result =
      VolumeManager::Call::SetPartitionLimit(fvm->device()->channel(), guid, kSliceSize * 2);
  ASSERT_OK(set_result.status(), "Transport layer error");

  // Validate the new value can be retrieved.
  VolumeManager::ResultOf::GetPartitionLimit get_result2 =
      VolumeManager::Call::GetPartitionLimit(fvm->device()->channel(), guid);
  ASSERT_OK(get_result2.status(), "Transport layer error");
  ASSERT_OK(get_result2->status, "Service returned error.");
  EXPECT_EQ(get_result2->byte_count, kSliceSize * 2, "Expected the limit we set.");

  // Find the partition we just created. Should be "<ramdisk-path>/fvm/<name>-p-1/block"
  fbl::unique_fd volume_fd;
  std::string device_name(ramdisk->path());
  device_name.append("/fvm/");
  device_name.append(kPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_->devfs_root(),
                                                          device_name.c_str(), &volume_fd));
  fdio_cpp::UnownedFdioCaller volume(volume_fd.get());

  // Try to expand it by one slice. Since the initial size was one slice and the limit is two, this
  // should succeed.
  Volume::ResultOf::Extend good_extend = Volume::Call::Extend(volume.channel()->borrow(), 100, 1);
  ASSERT_OK(good_extend.status(), "Transport error");
  ASSERT_OK(good_extend->status, "Expected Expand() call to succeed.");

  // Adding a third slice should fail since it's already at the max size.
  Volume::ResultOf::Extend bad_extend = Volume::Call::Extend(volume.channel()->borrow(), 200, 1);
  ASSERT_OK(bad_extend.status(), "Transport error");
  ASSERT_EQ(bad_extend->status, ZX_ERR_NO_SPACE, "Expected Expand() call to fail.");
}

}  // namespace
}  // namespace fvm
