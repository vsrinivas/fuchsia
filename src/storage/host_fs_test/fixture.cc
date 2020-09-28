// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/host_fs_test/fixture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <fbl/unique_fd.h>

#include "src/storage/minfs/fsck.h"

namespace fs_test {

constexpr off_t kDiskSize = 1llu << 32;

void HostFilesystemTest::SetUp() {
  mount_path_ = std::filesystem::temp_directory_path().append("host_fs_test.XXXXXX").string();
  auto fd = fbl::unique_fd(mkstemp(mount_path_.data()));
  ASSERT_TRUE(fd);

  ASSERT_EQ(ftruncate(fd.get(), kDiskSize), 0);

  fd.reset();

  ASSERT_EQ(emu_mkfs(mount_path_.c_str()), 0);

  ASSERT_EQ(emu_mount(mount_path_.c_str()), 0);
}

void HostFilesystemTest::TearDown() { EXPECT_EQ(unlink(mount_path_.c_str()), 0); }

int HostFilesystemTest::RunFsck() {
  fbl::unique_fd disk(open(mount_path_.c_str(), O_RDONLY));

  if (!disk) {
    std::cerr << "Unable to open disk for fsck" << std::endl;
    return -1;
  }

  struct stat stats;
  if (fstat(disk.get(), &stats) < 0) {
    std::cerr << "error: minfs could not find end of file/device" << std::endl;
    return 0;
  }

  if (stats.st_size != kDiskSize) {
    std::cerr << "Invalid disk" << std::endl;
    return -1;
  }

  size_t size = stats.st_size /= minfs::kMinfsBlockSize;

  std::unique_ptr<minfs::Bcache> block_cache;
  if (minfs::Bcache::Create(std::move(disk), static_cast<uint32_t>(size), &block_cache) != ZX_OK) {
    std::cerr << "error: cannot create block cache" << std::endl;
    return -1;
  }

  // The filesystem is never repaired on the host side.
  return Fsck(std::move(block_cache), minfs::FsckOptions());
}

}  // namespace fs_test
