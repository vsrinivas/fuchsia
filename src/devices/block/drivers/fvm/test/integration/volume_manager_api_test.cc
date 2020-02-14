// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <sys/types.h>

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

  const fvm::FormatInfo kExpectedFormat =
      fvm::FormatInfo::FromDiskSize(kBlockSize * kBlockCount, kSliceSize);

  llcpp::fuchsia::hardware::block::volume::VolumeManager::ResultOf::GetInfo result =
      llcpp::fuchsia::hardware::block::volume::VolumeManager::Call::GetInfo(
          fvm->device()->channel());

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result->status, "Service returned error.");

  // Check API returns the correct information for a non preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size(), result->info->slice_size);
  // Less or equal, because the metadata size is rounded to the nearest block boundary.
  EXPECT_LE(result->info->current_slice_count, result->info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.GetMaxAddressableSlices(kBlockSize * kBlockCount),
            result->info->current_slice_count);
  EXPECT_EQ(kExpectedFormat.GetMaxAllocatableSlices(), result->info->maximum_slice_count);
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

  const fvm::FormatInfo kExpectedFormat = fvm::FormatInfo::FromPreallocatedSize(
      kBlockSize * kBlockCount, kBlockSize * kMaxBlockCount, kSliceSize);

  llcpp::fuchsia::hardware::block::volume::VolumeManager::ResultOf::GetInfo result =
      llcpp::fuchsia::hardware::block::volume::VolumeManager::Call::GetInfo(
          fvm->device()->channel());

  ASSERT_OK(result.status(), "Transport layer error");
  ASSERT_OK(result->status, "Service returned error.");

  // Check API returns the correct information for a preallocated FVM.
  EXPECT_EQ(kExpectedFormat.slice_size(), result->info->slice_size);
  // Less than because we picked sizes that enforce a difference.
  EXPECT_LT(result->info->current_slice_count, result->info->maximum_slice_count);
  EXPECT_EQ(kExpectedFormat.slice_count(), result->info->current_slice_count);
  EXPECT_EQ(kExpectedFormat.GetMaxAllocatableSlices(), result->info->maximum_slice_count);
}

}  // namespace
}  // namespace fvm
