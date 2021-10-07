// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.feedback.testing/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <gtest/gtest.h>

#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/fshost_integration_test.h"
#include "src/storage/minfs/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/testing/zxcrypt.h"

namespace fshost {
namespace {

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kSliceSize = 32'768;
constexpr size_t kDeviceSize = kBlockCount * kBlockSize;

using FsRecoveryTest = FshostIntegrationTest;

TEST_F(FsRecoveryTest, EmptyPartitionRecoveryTest) {
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  // Now create the ram-disk with a single FVM partition.
  {
    auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    storage::FvmOptions options{
        .name = kDataPartitionLabel,
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  // Minfs should be automatically mounted.
  auto [fd, fs_type] = WaitForMount("minfs");
  EXPECT_TRUE(fd);
  EXPECT_TRUE(fs_type == VFS_TYPE_MINFS || fs_type == VFS_TYPE_FXFS);

  // No crash reports should have been filed.
  auto client_end = service::Connect<fuchsia_feedback_testing::FakeCrashReporterQuerier>();
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  auto client = fidl::BindSyncClient(std::move(*client_end));
  auto res = client.WatchFile();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res->num_filed, 0ul);
}

TEST_F(FsRecoveryTest, CorruptMinfsRecoveryTest) {
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  // Now create the ram-disk with a single FVM partition.
  {
    auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    storage::FvmOptions options{
        .name = kDataPartitionLabel,
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);

    auto zxcrypt_device_path_or = storage::CreateZxcryptVolume(fvm_partition_or.value());
    ASSERT_EQ(zxcrypt_device_path_or.status_value(), ZX_OK);
    std::string zxcrypt_device_path = std::move(zxcrypt_device_path_or.value());

    {
      ASSERT_EQ(
          mkfs(zxcrypt_device_path.c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, MkfsOptions()),
          ZX_OK);

      fbl::unique_fd minfs_fd(open(zxcrypt_device_path.c_str(), O_RDWR));
      ASSERT_TRUE(minfs_fd);
      // Write some garbage values to the block data bitmap. It's an empty minfs partition, so there
      // should be no block data, which means any values should trigger an fsck failure. We have to
      // write a whole block at a time or it throws errors.
      char garbage[minfs::kMinfsBlockSize] = {'g', 'a', 'r', 'b', 'a', 'g', 'e'};
      errno = 0;
      ASSERT_EQ(pwrite(minfs_fd.get(), garbage, sizeof(garbage),
                       minfs::kFVMBlockDataBmStart * minfs::kMinfsBlockSize),
                static_cast<ssize_t>(sizeof(garbage)))
          << "errno: " << strerror(errno);
    }

    // Confirm we messed it up enough to trigger an fsck failure.
    ASSERT_NE(
        fsck(zxcrypt_device_path.c_str(), DISK_FORMAT_MINFS, FsckOptions(), launch_stdio_sync),
        ZX_OK);
  }

  ResumeWatcher();

  // No crash reports should have been filed yet.
  auto client_end = service::Connect<fuchsia_feedback_testing::FakeCrashReporterQuerier>();
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  auto client = fidl::BindSyncClient(std::move(*client_end));
  auto res = client.WatchFile();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res->num_filed, 0ul);

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  // Minfs should be automatically mounted.
  auto [fd, fs_type] = WaitForMount("minfs");
  EXPECT_TRUE(fd);
  EXPECT_TRUE(fs_type == VFS_TYPE_MINFS || fs_type == VFS_TYPE_FXFS);

  // A crash report should have been filed with the crash reporting service.
  auto res2 = client.WatchFile();
  ASSERT_EQ(res2.status(), ZX_OK);
  ASSERT_EQ(res2->num_filed, 1ul);
}

}  // namespace
}  // namespace fshost
