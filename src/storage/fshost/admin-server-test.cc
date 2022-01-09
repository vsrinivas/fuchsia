// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/service/llcpp/service.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <gtest/gtest.h>

#include "sdk/lib/fdio/include/lib/fdio/spawn.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/fshost_integration_test.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

using AdminServerTest = FshostIntegrationTest;

void Join(const zx::process& process, int64_t* return_code) {
  *return_code = -1;

  ASSERT_EQ(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr), ZX_OK);

  zx_info_process_t proc_info{};
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr),
            ZX_OK);

  *return_code = proc_info.return_code;
}

TEST_F(AdminServerTest, MountAndUnmount) {
  auto ram_disk_or = storage::RamDisk::Create(1024, 64 * 1024);
  ASSERT_EQ(ram_disk_or.status_value(), ZX_OK);
  ASSERT_EQ(fs_management::Mkfs(ram_disk_or->path().c_str(), fs_management::kDiskFormatMinfs,
                                launch_stdio_sync, fs_management::MkfsOptions()),
            ZX_OK);

  std::string device_path = ram_disk_or->path();

  // Use the mount and umount binaries so that we get a full end-to-end test.
  constexpr const char* kMountBinPath = "/pkg/bin/mount";
  constexpr const char* kUmountBinPath = "/pkg/bin/umount";
  constexpr const char* kFshostSvcPath =
      "/hub/children/fshost-collection:test-fshost/exec/out/svc/fuchsia.fshost.Admin";
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

  constexpr const char* kFilePath =
      "/hub/children/fshost-collection:test-fshost/exec/out/fs/mnt/test/hello";
  fbl::unique_fd fd(open(kFilePath, O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "hello", 5), 5);
  fd.reset();

  // Check GetDevicePath.
  constexpr const char* kRoot = "/hub/children/fshost-collection:test-fshost/exec/out/fs/mnt/test/";
  struct statvfs buf;
  ASSERT_EQ(statvfs(kRoot, &buf), 0);

  auto fshost_or = service::Connect<fuchsia_fshost::Admin>(kFshostSvcPath);
  ASSERT_EQ(fshost_or.status_value(), ZX_OK);

  auto result = fidl::WireCall(*fshost_or)->GetDevicePath(buf.f_fsid);
  ASSERT_TRUE(result.ok());

  ASSERT_TRUE(result->result.is_response());
  EXPECT_EQ(result->result.response().path.get(), device_path);

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
  ASSERT_EQ(stat(kFilePath, &stat_buf), -1);

  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, kMountBinPath,
                       (const char*[]){kMountBinPath, "--fshost-path", kFshostSvcPath,
                                       device_path.c_str(), kMountPath, nullptr},
                       mount_process.reset_and_get_address()),
            ZX_OK);

  Join(mount_process, &return_code);
  ASSERT_EQ(return_code, 0);

  // Check the contents of the file.
  fd.reset(open(kFilePath, O_RDWR));
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

}  // namespace
}  // namespace fshost
