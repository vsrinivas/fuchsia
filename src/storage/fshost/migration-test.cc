// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.feedback.testing/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/lib/utils/topological_path.h"
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

using MigrationTest = testing::FshostIntegrationTest;

TEST_F(MigrationTest, MigratesZxcryptMinfs) {
  if (DataFilesystemFormat() != "fxfs" && DataFilesystemFormat() != "f2fs") {
    std::cerr << "Skipping test" << std::endl;
    return;
  }
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  constexpr char kFileContents[] = "to be, or not to be?";

  // Now create the ram-disk with a single FVM partition, formatted with zxcrypt, then minfs.
  std::string partition_path_suffix;
  {
    auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    storage::FvmOptions options{
        .name = kDataPartitionLabel,
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);
    auto fvm_topological_path_or = storage::GetTopologicalPath(*fvm_partition_or);
    ASSERT_EQ(fvm_topological_path_or.status_value(), ZX_OK);
    partition_path_suffix = fvm_topological_path_or->substr(ramdisk_or->path().length());

    auto zxcrypt_device_path_or = storage::CreateZxcryptVolume(fvm_partition_or.value());
    ASSERT_EQ(zxcrypt_device_path_or.status_value(), ZX_OK);

    ASSERT_EQ(fs_management::Mkfs(zxcrypt_device_path_or->c_str(),
                                  fs_management::DiskFormat::kDiskFormatMinfs,
                                  fs_management::LaunchStdioSync, fs_management::MkfsOptions{}),
              ZX_OK);

    // Mount the filesystem and add some data.
    auto device_fd = fbl::unique_fd(::open(zxcrypt_device_path_or->c_str(), O_RDONLY));
    ASSERT_TRUE(device_fd) << strerror(errno);
    auto mount =
        fs_management::Mount(std::move(device_fd), fs_management::kDiskFormatMinfs,
                             fs_management::MountOptions{}, fs_management::LaunchStdioAsync);
    ASSERT_EQ(mount.status_value(), ZX_OK);
    auto data = mount->DataRoot();
    ASSERT_EQ(data.status_value(), ZX_OK);
    auto binding = fs_management::NamespaceBinding::Create("/mnt/data", std::move(*data));
    ASSERT_EQ(binding.status_value(), ZX_OK);
    auto fd = fbl::unique_fd(::open("/mnt/data/file", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd) << strerror(errno);
    ASSERT_EQ(::write(fd.get(), kFileContents, strlen(kFileContents)),
              static_cast<ssize_t>(strlen(kFileContents)));
  }

  ResumeWatcher();

  // Now reattach the ram-disk.  Fshost should reformat the filesystem and copy the data into it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  // The filesystem should be automatically mounted.
  auto [root_fd, fs_type] = WaitForMount("data");
  EXPECT_TRUE(root_fd);
  EXPECT_EQ(fs_type, DataFilesystemFormat() == "fxfs"
                         ? fidl::ToUnderlying(fuchsia_fs::VfsType::kFxfs)
                         : fidl::ToUnderlying(fuchsia_fs::VfsType::kF2Fs));

  // The data should have been copied over.
  auto fd = fbl::unique_fd(::openat(root_fd.get(), "file", O_RDONLY));
  ASSERT_TRUE(fd) << strerror(errno);
  char buf[sizeof(kFileContents)] = {0};
  ASSERT_EQ(::read(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(strlen(kFileContents)));
  ASSERT_STREQ(buf, kFileContents);

  if (DataFilesystemFormat() == "fxfs") {
    // We should ensure the device isn't zxcrypt-formatted.
    std::string device_path = ramdisk_or->path() + partition_path_suffix;
    fprintf(stderr, "%s\n", device_path.c_str());
    struct stat st;
    ASSERT_TRUE(::stat(device_path.c_str(), &st) == 0)
        << "Failed to stat " << device_path << ": " << strerror(errno);
    std::string zxcrypt_path = device_path + "/zxcrypt";
    ASSERT_FALSE(::stat(zxcrypt_path.c_str(), &st) == 0) << zxcrypt_path << " shouldn't exist.";
  }

  // No crash reports should have been filed.
  auto client_end = component::Connect<fuchsia_feedback_testing::FakeCrashReporterQuerier>();
  ASSERT_EQ(client_end.status_value(), ZX_OK);
  fidl::WireSyncClient client{std::move(*client_end)};
  auto res = client->WatchFile();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res->num_filed, 0ul);
}

}  // namespace
}  // namespace fshost
