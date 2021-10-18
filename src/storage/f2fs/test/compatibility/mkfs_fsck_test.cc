// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>

#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
namespace {

std::string GenerateTestPath(std::string_view format) {
  std::string test_path = std::filesystem::temp_directory_path().append(format).string();
  return test_path;
}

TEST(CompatibilityTest, SimpleMkfsFsckTest) {
  uint64_t kBlockCount = 819200;
  uint64_t kDiskSize = kBlockCount * kDefaultSectorSize;

  std::string test_file_format = "f2fs_mkfs_fsck.XXXXXX";

  // Get test file
  std::string test_path = GenerateTestPath(test_file_format);
  auto fd = fbl::unique_fd(mkstemp(test_path.data()));
  ftruncate(fd.get(), kDiskSize);

  // mkfs on host
  std::string command = "mkfs.f2fs ";
  command.append(test_path);
  ASSERT_EQ(system(command.c_str()), 0);

  // fsck on Fuchsia
  std::unique_ptr<Bcache> bcache;
  ASSERT_EQ(Bcache::Create(std::move(fd), kBlockCount, &bcache), ZX_OK);
  ASSERT_EQ(Fsck(bcache.get()), ZX_OK);

  // Get test file
  std::string test_path2 = GenerateTestPath(test_file_format);
  auto fd2 = fbl::unique_fd(mkstemp(test_path2.data()));
  ftruncate(fd2.get(), kDiskSize);

  // mkfs on Fuchsia
  std::unique_ptr<Bcache> bcache2;
  ASSERT_EQ(Bcache::Create(std::move(fd2), kBlockCount, &bcache2), ZX_OK);
  MkfsOptions mkfs_options;
  MkfsWorker mkfs(std::move(bcache2), mkfs_options);
  auto ret = mkfs.DoMkfs();
  ASSERT_EQ(ret.is_error(), false);

  // fsck on host
  command = "fsck.f2fs ";
  command.append(test_path2);
  ASSERT_EQ(system(command.c_str()), 0);

  // Remove test files
  command = "rm ";
  command.append(test_path);
  command.append(" ");
  command.append(test_path2);
  ASSERT_EQ(system(command.c_str()), 0);
}

}  // namespace
}  // namespace f2fs
