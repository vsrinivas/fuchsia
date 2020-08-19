// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <zircon/device/block.h>

#include <fbl/unique_fd.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>
#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

namespace factoryfs {
namespace {

TEST(FactoryFs, ExportedFilesystemIsMountable) {
  constexpr int kDeviceBlockSize = 4096;
  constexpr int kBlockCount = 1024;

  auto ram_disk_or = isolated_devmgr::RamDisk::Create(kDeviceBlockSize, kBlockCount);
  ASSERT_TRUE(ram_disk_or.is_ok()) << ram_disk_or.status_string();

  char factoryfs_c_str[] = "/tmp/factoryfs.XXXXXX";
  ASSERT_NE(mkdtemp(factoryfs_c_str), nullptr);
  const std::string factoryfs(factoryfs_c_str);
  const std::string hello = factoryfs + "/hello";
  const std::string foo = factoryfs + "/foo";
  const std::string bar = factoryfs + "/foo/bar";
  fbl::unique_fd fd(open(hello.c_str(), O_CREAT | O_RDWR, 0777));
  ASSERT_TRUE(fd) << errno;
  ASSERT_EQ(write(fd.get(), "world", 5), 5);
  ASSERT_EQ(mkdir(foo.c_str(), 0777), 0);
  fd.reset(open(bar.c_str(), O_CREAT | O_RDWR, 0777));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "bar", 3), 3);
  fd.reset();

  std::string ram_disk_path = ram_disk_or.value().path();
  const char *argv[] = {"/pkg/bin/export_ffs", factoryfs.c_str(), ram_disk_path.c_str(), nullptr};
  zx::process process;
  zx_status_t status = fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                                  process.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  status = process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  ASSERT_EQ(status, ZX_OK);

  // Clean up.
  EXPECT_EQ(unlink(hello.c_str()), 0) << errno;
  EXPECT_EQ(unlink(bar.c_str()), 0) << errno;
  EXPECT_EQ(rmdir(foo.c_str()), 0) << errno;

  // Now try and mount Factoryfs.
  ASSERT_EQ(ramdisk_set_flags(ram_disk_or.value().client(), BLOCK_FLAG_READONLY), ZX_OK);

  fd.reset(open(ram_disk_path.c_str(), O_RDONLY));
  ASSERT_TRUE(fd) << errno;
  mount_options_t options = default_mount_options;
  options.register_fs = false;
  status =
      mount(fd.release(), factoryfs_c_str, DISK_FORMAT_FACTORYFS, &options, launch_stdio_async);
  EXPECT_EQ(status, ZX_OK);

  // And check contents of factoryfs.
  fd.reset(open(hello.c_str(), O_RDONLY));
  ASSERT_TRUE(fd) << errno;
  char buf[11];
  EXPECT_EQ(read(fd.get(), buf, sizeof(buf)), 5);
  EXPECT_EQ(memcmp(buf, "world", 5), 0);

  fd.reset(open(bar.c_str(), O_RDONLY));
  ASSERT_TRUE(fd) << errno;
  EXPECT_EQ(read(fd.get(), buf, sizeof(buf)), 3) << errno;
  EXPECT_EQ(memcmp(buf, "bar", 3), 0);
}

}  // namespace
}  // namespace factoryfs
