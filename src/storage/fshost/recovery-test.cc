// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.feedback.testing/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <cstddef>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/minfs/format.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/testing/zxcrypt.h"

namespace fshost {
namespace {

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kSliceSize = 32'768;
constexpr size_t kDeviceSize = static_cast<const size_t>(kBlockCount) * kBlockSize;

using FsRecoveryTest = testing::FshostIntegrationTest;

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
  auto [fd, fs_type] = WaitForMount("data");
  EXPECT_TRUE(fd);
  fuchsia_fs::VfsType expected_type;
  if (DataFilesystemFormat() == "minfs") {
    expected_type = fuchsia_fs::VfsType::kMinfs;
  } else if (DataFilesystemFormat() == "fxfs") {
    expected_type = fuchsia_fs::VfsType::kFxfs;
  } else if (DataFilesystemFormat() == "f2fs") {
    expected_type = fuchsia_fs::VfsType::kF2Fs;
  } else {
    ASSERT_TRUE(false);
  }
  EXPECT_EQ(fs_type, fidl::ToUnderlying(expected_type));

  // No crash reports should have been filed.
  auto client_end = component::Connect<fuchsia_feedback_testing::FakeCrashReporterQuerier>();
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireSyncClient client{std::move(*client_end)};
  auto res = client->WatchFile();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res.value().num_filed, 0ul);
}

TEST_F(FsRecoveryTest, CorruptDataRecoveryTest) {
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  {
    auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    storage::FvmOptions options{
        .name = kDataPartitionLabel,
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);

    std::string device_path = *fvm_partition_or;
    if (DataFilesystemFormat() != "fxfs") {
      auto zxcrypt_device_path_or = storage::CreateZxcryptVolume(fvm_partition_or.value());
      ASSERT_EQ(zxcrypt_device_path_or.status_value(), ZX_OK);
      device_path = std::move(zxcrypt_device_path_or.value());
    }

    // To make it look like there's a filesystem there but it is corrupt, write out the
    // appropriate magic into the otherwise empty block device.
    {
      fbl::unique_fd data_fd(open(device_path.c_str(), O_RDWR));
      ASSERT_TRUE(data_fd);
      char buf[4096];
      if (DataFilesystemFormat() == "minfs") {
        ::memcpy(buf, fs_management::kMinfsMagic, sizeof(fs_management::kMinfsMagic));
      } else if (DataFilesystemFormat() == "fxfs") {
        ::memcpy(buf, fs_management::kFxfsMagic, sizeof(fs_management::kFxfsMagic));
      } else if (DataFilesystemFormat() == "f2fs") {
        ::memcpy(buf, fs_management::kF2fsMagic, sizeof(fs_management::kF2fsMagic));
      } else {
        ASSERT_TRUE(false) << "Unsupported test configuration, data filesystem format: "
                           << DataFilesystemFormat();
      }
      ASSERT_EQ(pwrite(data_fd.get(), buf, sizeof(buf), 0), static_cast<ssize_t>(sizeof(buf)))
          << "errno: " << strerror(errno);
    }
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  // The new filesystem should be automatically mounted.
  auto [fd, fs_type] = WaitForMount("data");
  EXPECT_TRUE(fd);
  fuchsia_fs::VfsType expected_type;
  if (DataFilesystemFormat() == "minfs") {
    expected_type = fuchsia_fs::VfsType::kMinfs;
  } else if (DataFilesystemFormat() == "fxfs") {
    expected_type = fuchsia_fs::VfsType::kFxfs;
  } else if (DataFilesystemFormat() == "f2fs") {
    expected_type = fuchsia_fs::VfsType::kF2Fs;
  } else {
    ASSERT_TRUE(false);
  }
  EXPECT_EQ(fs_type, fidl::ToUnderlying(expected_type));

  // If fshost was configured to use (e.g.) Fxfs and the magic was Fxfs' magic, then fshost will
  // treat this as a corruption and file a crash report.  If the magic was something else, fshost
  // treats this as a first boot and just silently reformats.
  auto client_end = component::Connect<fuchsia_feedback_testing::FakeCrashReporterQuerier>();
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireSyncClient client{std::move(*client_end)};
  auto res = client->WatchFile();
  ASSERT_EQ(res.status(), ZX_OK);
  // Crash reporting is disabled for f2fs.
  unsigned long expected_num_filed = DataFilesystemFormat() == "f2fs" ? 0 : 1;
  ASSERT_EQ(res.value().num_filed, expected_num_filed);
}

}  // namespace
}  // namespace fshost
