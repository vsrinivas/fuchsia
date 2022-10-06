// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/component/cpp/service_client.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <cstddef>

#include <gtest/gtest.h>

#include "sdk/lib/fdio/include/lib/fdio/spawn.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

using AdminServerTest = testing::FshostIntegrationTest;

void Join(const zx::process& process, int64_t* return_code) {
  *return_code = -1;

  ASSERT_EQ(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr), ZX_OK);

  zx_info_process_t proc_info{};
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr),
            ZX_OK);

  *return_code = proc_info.return_code;
}

TEST_F(AdminServerTest, MountAndUnmount) {
  auto ram_disk_or = storage::RamDisk::Create(1024, 64L * 1024L);
  ASSERT_EQ(ram_disk_or.status_value(), ZX_OK);
  ASSERT_EQ(fs_management::Mkfs(ram_disk_or->path().c_str(), fs_management::kDiskFormatMinfs,
                                fs_management::LaunchStdioSync, fs_management::MkfsOptions()),
            ZX_OK);

  std::string device_path = ram_disk_or->path();

  constexpr const char* kFshostBindPath = "/fshost";
  constexpr const char* kFshostSvcPath = "/fshost/fuchsia.fshost.Admin";
  {
    // Bind Fshost's exposed directory into the namespace so the mount/umount binaries can reference
    // it.
    fdio_ns_t* ns = nullptr;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns, kFshostBindPath, exposed_dir().client_end().channel().get()), ZX_OK);
  }

  // Use the mount and umount binaries so that we get a full end-to-end test.
  constexpr const char* kMountBinPath = "/pkg/bin/mount";
  constexpr const char* kUmountBinPath = "/pkg/bin/umount";
  constexpr const char* kMountPath = "/mnt/test";

  zx::process mount_process;
  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kMountBinPath,
                       (const char*[]){kMountBinPath, "--fshost-path", kFshostSvcPath,
                                       device_path.c_str(), kMountPath, nullptr},
                       mount_process.reset_and_get_address()),
            ZX_OK);

  int64_t return_code;
  Join(mount_process, &return_code);
  ASSERT_EQ(return_code, 0);

  const std::string file_path = std::string(kFshostBindPath) + "/mnt/test/hello";
  fbl::unique_fd fd(open(file_path.c_str(), O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "hello", 5), 5);
  fd.reset();

  // Check GetDevicePath.
  const std::string root = std::string(kFshostBindPath) + "/mnt/test/";
  struct statvfs buf;
  ASSERT_EQ(statvfs(root.c_str(), &buf), 0);

  auto fshost_or = component::Connect<fuchsia_fshost::Admin>(kFshostSvcPath);
  ASSERT_EQ(fshost_or.status_value(), ZX_OK);

  auto result = fidl::WireCall(*fshost_or)->GetDevicePath(buf.f_fsid);
  ASSERT_TRUE(result.ok());

  ASSERT_TRUE(result->is_ok());
  EXPECT_EQ(result->value()->path.get(), device_path);

  zx::process umount_process;
  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kUmountBinPath,
                       (const char*[]){kUmountBinPath, "--fshost-path", kFshostSvcPath, kMountPath,
                                       nullptr},
                       umount_process.reset_and_get_address()),
            ZX_OK);

  Join(umount_process, &return_code);
  ASSERT_EQ(return_code, 0);

  // The file should no longer exist.
  struct stat stat_buf;
  ASSERT_EQ(stat(file_path.c_str(), &stat_buf), -1);

  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kMountBinPath,
                       (const char*[]){kMountBinPath, "--fshost-path", kFshostSvcPath,
                                       device_path.c_str(), kMountPath, nullptr},
                       mount_process.reset_and_get_address()),
            ZX_OK);

  Join(mount_process, &return_code);
  ASSERT_EQ(return_code, 0);

  // Check the contents of the file.
  fd.reset(open(file_path.c_str(), O_RDWR));
  ASSERT_TRUE(fd);
  char buffer[5];
  ASSERT_EQ(read(fd.get(), buffer, 5), 5);
  ASSERT_EQ(memcmp(buffer, "hello", 5), 0);

  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kUmountBinPath,
                       (const char*[]){kUmountBinPath, "--fshost-path", kFshostSvcPath, kMountPath,
                                       nullptr},
                       umount_process.reset_and_get_address()),
            ZX_OK);

  Join(umount_process, &return_code);
  ASSERT_EQ(return_code, 0);
}

TEST_F(AdminServerTest, GetDevicePathForBuiltInFilesystem) {
  constexpr uint32_t kBlockCount = 9 * 1024 * 256;
  constexpr uint32_t kBlockSize = 512;
  constexpr uint32_t kSliceSize = 32'768;
  constexpr size_t kDeviceSize = static_cast<const size_t>(kBlockCount) * kBlockSize;

  constexpr const char* kFshostBindPath = "/fshost";
  constexpr const char* kFshostSvcPath = "/fshost/fuchsia.fshost.Admin";
  {
    // Bind Fshost's exposed directory into the namespace so the mount/umount binaries can reference
    // it.
    fdio_ns_t* ns = nullptr;
    ASSERT_EQ(fdio_ns_get_installed(&ns), ZX_OK);
    ASSERT_EQ(fdio_ns_bind(ns, kFshostBindPath, exposed_dir().client_end().channel().get()), ZX_OK);
  }

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
  auto [fd, fs_type] = WaitForMount("data");
  ASSERT_TRUE(fd);
  auto expected_fs_type = fuchsia_fs::VfsType::kMinfs;
  if (DataFilesystemFormat() == "f2fs") {
    expected_fs_type = fuchsia_fs::VfsType::kF2Fs;
  } else if (DataFilesystemFormat() == "fxfs") {
    expected_fs_type = fuchsia_fs::VfsType::kFxfs;
  }
  EXPECT_EQ(fs_type, fidl::ToUnderlying(expected_fs_type));

  struct statvfs buf;
  ASSERT_EQ(fstatvfs(fd.get(), &buf), 0);

  auto fshost_or = component::Connect<fuchsia_fshost::Admin>(kFshostSvcPath);
  ASSERT_EQ(fshost_or.status_value(), ZX_OK);

  // The device path is registered in fshost *after* the mount point shows up so this is racy.  It's
  // not worth fixing fshost since the device path is used for debugging/diagnostics, so we just
  // loop here.
  int attempts = 0;
  for (;;) {
    auto result = fidl::WireCall(*fshost_or)->GetDevicePath(buf.f_fsid);
    ASSERT_TRUE(result.ok());
    if (result.value().is_error()) {
      if (++attempts == 100)
        GTEST_FAIL() << "Timed out trying to get device path";
      usleep(100'000);
    } else {
      if (DataFilesystemFormat() == "fxfs") {
        // Fxfs doesn't use zxcrypt.
        EXPECT_EQ(result.value().value()->path.get(), ramdisk_or->path() + "/fvm/data-p-1/block");
      } else {
        EXPECT_EQ(result.value().value()->path.get(),
                  ramdisk_or->path() + "/fvm/data-p-1/block/zxcrypt/unsealed/block");
      }
      break;
    }
  }
}

}  // namespace
}  // namespace fshost
