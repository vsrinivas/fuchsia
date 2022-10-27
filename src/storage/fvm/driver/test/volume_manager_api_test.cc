// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
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
  static void SetUpTestSuite() {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    devmgr_ = std::make_unique<IsolatedDevmgr>();
    ASSERT_OK(IsolatedDevmgr::Create(&args, devmgr_.get()));
  }

  static void TearDownTestSuite() { devmgr_.reset(); }

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

  fdio_cpp::UnownedFdioCaller caller(fvm->device()->devfs_root_fd());
  zx::result channel =
      component::ConnectAt<VolumeManager>(caller.directory(), fvm->device()->path());
  ASSERT_OK(channel.status_value());

  fidl::WireResult result = fidl::WireCall(channel.value())->GetInfo();

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result.value().status, "Service returned error.");

  // Check API returns the correct information for a non preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size, result.value().info->slice_size);
  // Less or equal, because the metadata size is rounded to the nearest block boundary.
  EXPECT_LE(result.value().info->slice_count, result.value().info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.GetMaxAllocationTableEntriesForDiskSize(kBlockSize * kBlockCount),
            result.value().info->slice_count);
  EXPECT_EQ(kExpectedFormat.GetAllocationTableAllocatedEntryCount(),
            result.value().info->maximum_slice_count);
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

  fdio_cpp::UnownedFdioCaller caller(fvm->device()->devfs_root_fd());
  zx::result channel =
      component::ConnectAt<VolumeManager>(caller.directory(), fvm->device()->path());
  ASSERT_OK(channel.status_value());

  fidl::WireResult result = fidl::WireCall(channel.value())->GetInfo();

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result.value().status, "Service returned error.");

  // Check API returns the correct information for a preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size, result.value().info->slice_size);
  // Less than because we picked sizes that enforce a difference.
  EXPECT_LT(result.value().info->slice_count, result.value().info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.pslice_count, result.value().info->slice_count);
  EXPECT_EQ(kExpectedFormat.GetAllocationTableAllocatedEntryCount(),
            result.value().info->maximum_slice_count);
  EXPECT_EQ(0, result.value().info->assigned_slice_count);
}

// Tests that the maximum extents apply to partition growth properly. This also tests the
// basics of the GetVolumeInfo() call.
TEST_F(FvmVolumeManagerApiTest, PartitionLimit) {
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

  // Type GUID for partition.
  fuchsia_hardware_block_partition::wire::Guid type_guid;
  std::fill(std::begin(type_guid.value), std::end(type_guid.value), 0x11);

  // Instance GUID for partition.
  fuchsia_hardware_block_partition::wire::Guid guid;
  std::fill(std::begin(guid.value), std::end(guid.value), 0x12);

  fdio_cpp::UnownedFdioCaller caller(fvm->device()->devfs_root_fd());
  zx::result channel =
      component::ConnectAt<VolumeManager>(caller.directory(), fvm->device()->path());
  ASSERT_OK(channel.status_value());

  // The partition hasn't been created yet, the result should be "not found".
  fidl::WireResult<VolumeManager::GetPartitionLimit> unfound_result =
      fidl::WireCall(channel.value())->GetPartitionLimit(guid);
  ASSERT_OK(unfound_result.status(), "Transport layer error");
  ASSERT_EQ(unfound_result.value().status, ZX_ERR_NOT_FOUND);

  // Create the partition inside FVM with one slice.
  const char kPartitionName[] = "mypart";
  fidl::WireResult<VolumeManager::AllocatePartition> alloc_result =
      fidl::WireCall(channel.value())->AllocatePartition(1, type_guid, guid, kPartitionName, 0);
  ASSERT_OK(alloc_result.status(), "Transport layer error");
  ASSERT_OK(alloc_result.value().status, "Service returned error.");

  // Find the partition we just created. Should be "<ramdisk-path>/fvm/<name>-p-1/block"
  fbl::unique_fd volume_fd;
  std::string device_name(ramdisk->path());
  device_name.append("/fvm/");
  device_name.append(kPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(
      device_watcher::RecursiveWaitForFile(devmgr_->devfs_root(), device_name.c_str(), &volume_fd));
  fdio_cpp::UnownedFdioCaller volume(volume_fd.get());

  // Query the volume to check its information.
  fidl::WireResult<Volume::GetVolumeInfo> get_info =
      fidl::WireCall(volume.borrow_as<Volume>())->GetVolumeInfo();
  ASSERT_OK(get_info.status(), "Transport error");
  ASSERT_OK(get_info.value().status, "Expected GetVolumeInfo() call to succeed.");
  EXPECT_EQ(kSliceSize, get_info.value().manager->slice_size);
  EXPECT_EQ(kExpectedFormat.pslice_count, get_info.value().manager->slice_count);
  EXPECT_EQ(1u, get_info.value().manager->assigned_slice_count);
  EXPECT_EQ(1u, get_info.value().volume->partition_slice_count);
  EXPECT_EQ(0u, get_info.value().volume->slice_limit);

  // That partition's initial limit should be 0 (no limit).
  fidl::WireResult<VolumeManager::GetPartitionLimit> get_result =
      fidl::WireCall(channel.value())->GetPartitionLimit(guid);
  ASSERT_OK(get_result.status(), "Transport layer error");
  ASSERT_OK(get_result.value().status, "Service returned error.");
  EXPECT_EQ(get_result.value().slice_count, 0, "Expected 0 limit on init.");

  // Set the limit to two slices.
  fidl::WireResult<VolumeManager::SetPartitionLimit> set_result =
      fidl::WireCall(channel.value())->SetPartitionLimit(guid, 2);
  ASSERT_OK(set_result.status(), "Transport layer error");

  // Validate the new value can be retrieved.
  fidl::WireResult<VolumeManager::GetPartitionLimit> get_result2 =
      fidl::WireCall(channel.value())->GetPartitionLimit(guid);
  ASSERT_OK(get_result2.status(), "Transport layer error");
  ASSERT_OK(get_result2.value().status, "Service returned error.");
  EXPECT_EQ(get_result2.value().slice_count, 2, "Expected the limit we set.");

  // Try to expand it by one slice. Since the initial size was one slice and the limit is two, this
  // should succeed.
  fidl::WireResult<Volume::Extend> good_extend =
      fidl::WireCall(volume.borrow_as<Volume>())->Extend(100, 1);
  ASSERT_OK(good_extend.status(), "Transport error");
  ASSERT_OK(good_extend.value().status, "Expected Expand() call to succeed.");

  // Query the volume to check its information.
  fidl::WireResult<Volume::GetVolumeInfo> get_info2 =
      fidl::WireCall(volume.borrow_as<Volume>())->GetVolumeInfo();
  ASSERT_OK(get_info2.status(), "Transport error");
  ASSERT_OK(get_info2.value().status, "Expected GetVolumeInfo() call to succeed.");
  EXPECT_EQ(kSliceSize, get_info2.value().manager->slice_size);
  EXPECT_EQ(kExpectedFormat.pslice_count, get_info2.value().manager->slice_count);
  EXPECT_EQ(2u, get_info2.value().manager->assigned_slice_count);
  EXPECT_EQ(2u, get_info2.value().volume->partition_slice_count);
  EXPECT_EQ(2u, get_info2.value().volume->slice_limit);

  // Adding a third slice should fail since it's already at the max size.
  fidl::WireResult<Volume::Extend> bad_extend =
      fidl::WireCall(volume.borrow_as<Volume>())->Extend(200, 1);
  ASSERT_OK(bad_extend.status(), "Transport error");
  ASSERT_EQ(bad_extend.value().status, ZX_ERR_NO_SPACE, "Expected Expand() call to fail.");

  // Delete and re-create the partition. It should have no limit.
  fidl::WireResult<Volume::Destroy> destroy_result =
      fidl::WireCall(volume.borrow_as<Volume>())->Destroy();
  ASSERT_OK(destroy_result.status(), "Transport layer error");
  ASSERT_OK(destroy_result.value().status, "Can't destroy partition.");
  volume_fd.reset();

  fidl::WireResult<VolumeManager::AllocatePartition> alloc2_result =
      fidl::WireCall(channel.value())
          ->AllocatePartition(1, type_guid, guid,
                              /*kPartitionName*/ "thepart", 0);
  ASSERT_OK(alloc2_result.status(), "Transport layer error");
  ASSERT_OK(alloc2_result.value().status, "Service returned error.");

  // That partition's initial limit should be 0 (no limit).
  fidl::WireResult<VolumeManager::GetPartitionLimit> last_get_result =
      fidl::WireCall(channel.value())->GetPartitionLimit(guid);
  ASSERT_OK(last_get_result.status(), "Transport layer error");
  ASSERT_OK(last_get_result.value().status, "Service returned error.");
  EXPECT_EQ(last_get_result.value().slice_count, 0, "Expected 0 limit on new partition.");
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

  fdio_cpp::UnownedFdioCaller caller(fvm->device()->devfs_root_fd());
  zx::result channel =
      component::ConnectAt<VolumeManager>(caller.directory(), fvm->device()->path());
  ASSERT_OK(channel.status_value());

  // Create the partition inside FVM with one slice.
  const char kPartitionName[] = "mypart";
  auto alloc_result =
      fidl::WireCall(channel.value())->AllocatePartition(1, type_guid, guid, kPartitionName, 0);
  ASSERT_OK(alloc_result.status(), "Transport layer error");
  ASSERT_OK(alloc_result.value().status, "Service returned error.");

  constexpr std::string_view kNewPartitionName = "new-name";
  auto set_partition_name_result =
      fidl::WireCall(channel.value())
          ->SetPartitionName(guid, fidl::StringView::FromExternal(kNewPartitionName));
  ASSERT_OK(set_partition_name_result.status(), "Transport layer error");
  ASSERT_FALSE(set_partition_name_result->is_error(), "Service returned error.");

  // Find the partition we just created. It will still have the original path:
  // "<ramdisk-path>/fvm/mypart-p-1/block"
  fbl::unique_fd volume_fd;
  std::string device_name(ramdisk->path());
  device_name.append("/fvm/");
  device_name.append(kPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(
      device_watcher::RecursiveWaitForFile(devmgr_->devfs_root(), device_name.c_str(), &volume_fd));
  fdio_cpp::UnownedFdioCaller volume(volume_fd.get());

  {
    auto get_name_result =
        fidl::WireCall(fidl::UnownedClientEnd<Volume>(volume.borrow_channel()))->GetName();
    ASSERT_OK(get_name_result.status(), "Transport layer error");
    ASSERT_OK(get_name_result.value().status, "Service returned error.");

    ASSERT_EQ(get_name_result.value().name.get(), kNewPartitionName);
  }

  // Make sure that the change was persisted.
  volume_fd.reset();
  ASSERT_OK(fvm->Rebind({}));

  // This time, the path should include the new name.
  device_name = ramdisk->path();
  device_name.append("/fvm/");
  device_name.append(kNewPartitionName);
  device_name.append("-p-1/block");
  ASSERT_OK(
      device_watcher::RecursiveWaitForFile(devmgr_->devfs_root(), device_name.c_str(), &volume_fd));
  volume.reset(volume_fd.get());

  auto get_name_result =
      fidl::WireCall(fidl::UnownedClientEnd<Volume>(volume.borrow_channel()))->GetName();
  ASSERT_OK(get_name_result.status(), "Transport layer error");
  ASSERT_OK(get_name_result.value().status, "Service returned error.");

  ASSERT_EQ(get_name_result.value().name.get(), kNewPartitionName);
}

}  // namespace
}  // namespace fvm
