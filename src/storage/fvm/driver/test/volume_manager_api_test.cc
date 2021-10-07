// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/test_support.h"

namespace fvm {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kSliceSize = 1 << 20;

using driver_integration_test::IsolatedDevmgr;

// using Partition = fuchsia_hardware_block_partition::Partition;
using Volume = fuchsia_hardware_block_volume::Volume;
using VolumeManager = fuchsia_hardware_block_volume::VolumeManager;

class FvmVolumeManagerApiTest : public zxtest::Test {
 public:
  static void SetUpTestCase() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    devmgr_ = std::make_unique<IsolatedDevmgr>();
    ASSERT_OK(IsolatedDevmgr::Create(&args, devmgr_.get()));
  }

  static void TearDownTestCase() { devmgr_.reset(); }

 protected:
  static std::unique_ptr<IsolatedDevmgr> devmgr_;
};

std::unique_ptr<IsolatedDevmgr> FvmVolumeManagerApiTest::devmgr_ = nullptr;

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

  fidl::WireResult<VolumeManager::GetInfo> result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetInfo();

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

  fidl::WireResult<VolumeManager::GetInfo> result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetInfo();

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
  fuchsia_hardware_block_partition::wire::Guid type_guid;
  std::fill(std::begin(type_guid.value), std::end(type_guid.value), 0x11);

  // Instance GUID for partition.
  fuchsia_hardware_block_partition::wire::Guid guid;
  std::fill(std::begin(guid.value), std::end(guid.value), 0x12);

  // The partition hasn't been created yet, the result should be "not found".
  fidl::WireResult<VolumeManager::GetPartitionLimit> unfound_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetPartitionLimit(guid);
  ASSERT_OK(unfound_result.status(), "Transport layer error");
  ASSERT_EQ(unfound_result->status, ZX_ERR_NOT_FOUND);

  // Create the partition inside FVM with one slice.
  const char kPartitionName[] = "mypart";
  fidl::WireResult<VolumeManager::AllocatePartition> alloc_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())
          ->AllocatePartition(1, type_guid, guid, kPartitionName, 0);
  ASSERT_OK(alloc_result.status(), "Transport layer error");
  ASSERT_OK(alloc_result->status, "Service returned error.");

  // That partition's initial limit should be 0 (no limit).
  fidl::WireResult<VolumeManager::GetPartitionLimit> get_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetPartitionLimit(guid);
  ASSERT_OK(get_result.status(), "Transport layer error");
  ASSERT_OK(get_result->status, "Service returned error.");
  EXPECT_EQ(get_result->byte_count, 0, "Expected 0 limit on init.");

  // Set the limit to two slices.
  fidl::WireResult<VolumeManager::SetPartitionLimit> set_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())
          ->SetPartitionLimit(guid, kSliceSize * 2);
  ASSERT_OK(set_result.status(), "Transport layer error");

  // Validate the new value can be retrieved.
  fidl::WireResult<VolumeManager::GetPartitionLimit> get_result2 =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetPartitionLimit(guid);
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
  fidl::WireResult<Volume::Extend> good_extend =
      fidl::WireCall<Volume>(volume.channel()->borrow())->Extend(100, 1);
  ASSERT_OK(good_extend.status(), "Transport error");
  ASSERT_OK(good_extend->status, "Expected Expand() call to succeed.");

  // Adding a third slice should fail since it's already at the max size.
  fidl::WireResult<Volume::Extend> bad_extend =
      fidl::WireCall<Volume>(volume.channel()->borrow())->Extend(200, 1);
  ASSERT_OK(bad_extend.status(), "Transport error");
  ASSERT_EQ(bad_extend->status, ZX_ERR_NO_SPACE, "Expected Expand() call to fail.");

  // Delete and re-create the partition. It should have no limit.
  fidl::WireResult<Volume::Destroy> destroy_result =
      fidl::WireCall<Volume>(volume.channel()->borrow())->Destroy();
  ASSERT_OK(destroy_result.status(), "Transport layer error");
  ASSERT_OK(destroy_result->status, "Can't destroy partition.");
  volume_fd.reset();

  fidl::WireResult<VolumeManager::AllocatePartition> alloc2_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())
          ->AllocatePartition(1, type_guid, guid,
                              /*kPartitionName*/ "thepart", 0);
  ASSERT_OK(alloc2_result.status(), "Transport layer error");
  ASSERT_OK(alloc2_result->status, "Service returned error.");

  // That partition's initial limit should be 0 (no limit).
  fidl::WireResult<VolumeManager::GetPartitionLimit> last_get_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())->GetPartitionLimit(guid);
  ASSERT_OK(last_get_result.status(), "Transport layer error");
  ASSERT_OK(last_get_result->status, "Service returned error.");
  EXPECT_EQ(last_get_result->byte_count, 0, "Expected 0 limit on new partition.");
}

TEST_F(FvmVolumeManagerApiTest, SetPartitionName) {
  constexpr uint64_t kBlockCount = (50 * kSliceSize) / kBlockSize;
  constexpr uint64_t kMaxBlockCount = 1024 * kSliceSize / kBlockSize;

  std::unique_ptr<RamdiskRef> ramdisk =
      RamdiskRef::Create(devmgr_->devfs_root(), kBlockSize, kBlockCount);
  ASSERT_TRUE(ramdisk);

  std::unique_ptr<FvmAdapter> fvm = FvmAdapter::CreateGrowable(
      devmgr_->devfs_root(), kBlockSize, kBlockCount, kMaxBlockCount, kSliceSize, ramdisk.get());
  ASSERT_TRUE(fvm);

  // Type GUID for partition.
  fuchsia_hardware_block_partition::wire::Guid type_guid;
  std::fill(std::begin(type_guid.value), std::end(type_guid.value), 0x11);

  // Instance GUID for partition.
  fuchsia_hardware_block_partition::wire::Guid guid;
  std::fill(std::begin(guid.value), std::end(guid.value), 0x12);

  // Create the partition inside FVM with one slice.
  const char kPartitionName[] = "mypart";
  auto alloc_result = fidl::WireCall<VolumeManager>(fvm->device()->channel())
                          .AllocatePartition(1, type_guid, guid, kPartitionName, 0);
  ASSERT_OK(alloc_result.status(), "Transport layer error");
  ASSERT_OK(alloc_result->status, "Service returned error.");

  constexpr std::string_view kNewPartitionName = "new-name";
  auto set_partition_name_result =
      fidl::WireCall<VolumeManager>(fvm->device()->channel())
          .SetPartitionName(guid, fidl::StringView::FromExternal(kNewPartitionName));
  ASSERT_OK(set_partition_name_result.status(), "Transport layer error");
  ASSERT_FALSE(set_partition_name_result->result.is_err(), "Service returned error.");

  // Find the partition we just created. It will still have the original path:
  // "<ramdisk-path>/fvm/mypart-p-1/block"
  fbl::unique_fd volume_fd;
  std::string device_name(ramdisk->path());
  device_name.append("/fvm/");
  device_name.append(kPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_->devfs_root(),
                                                          device_name.c_str(), &volume_fd));
  fdio_cpp::UnownedFdioCaller volume(volume_fd.get());

  {
    auto get_name_result =
        fidl::WireCall(fidl::UnownedClientEnd<Volume>(volume.borrow_channel())).GetName();
    ASSERT_OK(get_name_result.status(), "Transport layer error");
    ASSERT_OK(get_name_result->status, "Service returned error.");

    ASSERT_EQ(get_name_result->name.get(), kNewPartitionName);
  }

  // Make sure that the change was persisted.
  volume_fd.reset();
  ASSERT_OK(fvm->Rebind({}));

  // This time, the path should include the new name.
  device_name = ramdisk->path();
  device_name.append("/fvm/");
  device_name.append(kNewPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr_->devfs_root(),
                                                          device_name.c_str(), &volume_fd));
  volume.reset(volume_fd.get());

  auto get_name_result =
      fidl::WireCall(fidl::UnownedClientEnd<Volume>(volume.borrow_channel())).GetName();
  ASSERT_OK(get_name_result.status(), "Transport layer error");
  ASSERT_OK(get_name_result->status, "Service returned error.");

  ASSERT_EQ(get_name_result->name.get(), kNewPartitionName);
}

}  // namespace
}  // namespace fvm
