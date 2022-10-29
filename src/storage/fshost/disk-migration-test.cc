// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/statfs.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/testing/zxcrypt.h"
#include "zircon/errors.h"

namespace fshost {
namespace {

// The size of device blocks.
constexpr uint32_t kBlockSize = 512;
// The size of an FVM slice.
constexpr uint32_t kSliceSize = 32'768;

namespace volume = fuchsia_hardware_block_volume;

// For tests that want the full integration test suite.
using DataMigrationIntegration = testing::FshostIntegrationTest;

// Writes a disk image to the provided vmo.
// The image is an FVM container with a single minfs partition containing a 4MiB file.
void BuildDiskImage(zx::vmo vmo) {
  uint64_t vmo_size = 0;
  ASSERT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  const uint32_t block_count = static_cast<uint32_t>(vmo_size) / kBlockSize;

  auto ramdisk = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk.status_value(), ZX_OK);
  storage::FvmOptions options{
      .name = kDataPartitionLabel,
      .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
  };
  auto fvm_partition = storage::CreateFvmPartition(ramdisk->path(), kSliceSize, options);
  ASSERT_EQ(fvm_partition.status_value(), ZX_OK);

  // Create a zxcrypt volume in the partition.
  auto zxcrypt = storage::CreateZxcryptVolume(fvm_partition.value());
  ASSERT_EQ(zxcrypt.status_value(), ZX_OK);

  // Format the new fvm partition with minfs.
  {
    FX_LOGS(INFO) << "Formatting \"" << zxcrypt.value() << "\" as minfs.";
    zx::result channel = component::Connect<fuchsia_hardware_block::Block>(zxcrypt.value());
    ASSERT_TRUE(channel.is_ok()) << channel.status_string();
    std::unique_ptr<block_client::RemoteBlockDevice> minfs_device;
    ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(channel.value()), &minfs_device),
              ZX_OK);
    auto bc = minfs::Bcache::Create(std::move(minfs_device), block_count);
    ASSERT_EQ(bc.status_value(), ZX_OK);
    ASSERT_EQ(minfs::Mkfs(bc.value().get()).status_value(), ZX_OK);

    // Write a simple file hierarchy out to test the copy code.
    FX_LOGS(INFO) << "Mounting as minfs.";
    zx::result endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    zx_status_t status = ZX_OK;
    std::thread thread([&status, &endpoints]() {
      auto client = std::move(endpoints->client);
      auto root = fs_management::FsRootHandle(client);
      ASSERT_EQ(root.status_value(), ZX_OK);
      fbl::unique_fd fd;
      ASSERT_EQ(fdio_fd_create(root.value().TakeChannel().release(), fd.reset_and_get_address()),
                ZX_OK);
      if (!fd) {
        FX_LOGS(ERROR) << "Failed to create fd.";
        status = ZX_ERR_INTERNAL;
        return;
      }
      if (mkdirat(fd.get(), "somedir", 0755) != 0) {
        FX_LOGS(ERROR) << "Failed to make directory:" << errno;
        status = ZX_ERR_INTERNAL;
        return;
      }
      fbl::unique_fd dir_fd{openat(fd.get(), "somedir", O_RDWR | O_DIRECTORY)};
      if (!dir_fd) {
        FX_LOGS(ERROR) << "Failed to open directory.";
        status = ZX_ERR_INTERNAL;
        return;
      }
      int file_fd = openat(dir_fd.get(), "file.txt", O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
      if (file_fd <= 0) {
        FX_LOGS(ERROR) << "Failed to open minfs file.";
        status = ZX_ERR_INTERNAL;
        return;
      }

      // Write out a 4MiB file.
      for (int i = 0; i < 4 * 1024; i++) {
        std::array<char, 1024> buf;
        buf.fill(static_cast<char>(i));
        write(file_fd, buf.data(), buf.size());
      }
      close(file_fd);
    });
    ASSERT_EQ(minfs::Mount(std::move(bc.value()), minfs::MountOptions(),
                           std::move(endpoints.value().server))
                  .status_value(),
              ZX_OK);
    thread.join();
    ASSERT_EQ(status, ZX_OK);
  }
}

void CheckFilesystem(storage::RamDisk ramdisk, fbl::unique_fd fd, bool expect_disk_migration) {
  ASSERT_TRUE(fd);

  // FVM will be at something like "/dev/sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm"
  std::string fvm_path = ramdisk.path() + "/fvm";

  std::string partition_path;
  if (expect_disk_migration) {
    // The fxfs partition will be the only active partition inside FVM.
    // Inactive partitions don't get exported so we should only see data-p-2.
    partition_path = fvm_path + "/" + std::string(kDataPartitionLabel) + "-p-2/block";
  } else {
    // Otherwise we've reused the partition we had minfs on.
    // If we've migrated via the RAM path, this will be Fxfs. If we have failed the
    // disk-based migration path, this will still be Minfs.
    partition_path = fvm_path + "/" + std::string(kDataPartitionLabel) + "-p-1/block";
  }
  FX_LOGS(INFO) << "Checking partition: " << partition_path;
  fbl::unique_fd partition_fd(open(partition_path.c_str(), O_RDONLY));
  ASSERT_TRUE(partition_fd);

  // Query the partition name.
  fdio_cpp::UnownedFdioCaller partition_caller(partition_fd.get());
  auto result = fidl::WireCall(partition_caller.borrow_as<volume::Volume>())->GetName();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.value().status, ZX_OK);

  // It should be the preferred name.
  ASSERT_EQ(result.value().name.get(), kDataPartitionLabel);

  // The file contents should be the same.
  fbl::unique_fd file_fd(openat(fd.get(), "somedir/file.txt", O_RDONLY));
  ASSERT_TRUE(file_fd);
  // Read back our a 4MiB file.
  for (int i = 0; i < 4 * 1024; i++) {
    std::array<char, 1024> buf;
    std::array<char, 1024> expected;
    expected.fill(static_cast<char>(i));
    read(file_fd.get(), buf.data(), buf.size());
    ASSERT_EQ(buf, expected);
  }
}

TEST_F(DataMigrationIntegration, Success) {
  ASSERT_EQ(DataFilesystemFormat(), "fxfs");
  constexpr size_t kDeviceSize = 256L << 20;  // 128MiB

  PauseWatcher();  // Pause whilst we create a ramdisk.

  zx::vmo vmo;
  zx::vmo child_vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);
  BuildDiskImage(std::move(child_vmo));

  ResumeWatcher();

  // Reattach the ram-disk and fshost should migrate the minfs to fxfs.
  auto ramdisk = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk.status_value(), ZX_OK);
  auto [fd, fs_type] = WaitForMount("data");
  EXPECT_EQ(fs_type, fidl::ToUnderlying(fuchsia_fs::VfsType::kFxfs));
  CheckFilesystem(std::move(ramdisk.value()), std::move(fd), true);

  auto inspect = TakeSnapshot();
  EXPECT_EQ(inspect.GetByPath({"migration_status"})
                ->node()
                .get_property<inspect::IntPropertyValue>("success")
                ->value(),
            1);
}

TEST_F(DataMigrationIntegration, InsufficientDiskFallback) {
  ASSERT_EQ(DataFilesystemFormat(), "fxfs");
  constexpr size_t kDeviceSize = 8L << 20;  // 8MiB

  PauseWatcher();  // Pause whilst we create a ramdisk.

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Build the pre-migration disk image using a child VMO so we don't consume
  // the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);
  BuildDiskImage(std::move(child_vmo));

  ResumeWatcher();

  // Reattach the ram-disk. migration should fail and we should get our minfs partition.
  auto ramdisk = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk.status_value(), ZX_OK);
  auto [fd, fs_type] = WaitForMount("data");
  EXPECT_EQ(fs_type, fidl::ToUnderlying(fuchsia_fs::VfsType::kMinfs));
  CheckFilesystem(std::move(ramdisk.value()), std::move(fd), false);

  auto inspect = TakeSnapshot();
  EXPECT_EQ(inspect.GetByPath({"migration_status"})
                ->node()
                .get_property<inspect::IntPropertyValue>("out_of_space")
                ->value(),
            1);
}

}  // namespace
}  // namespace fshost
