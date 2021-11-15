// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/compatibility.h"

namespace f2fs {
namespace {

TEST(CompatibilityTest, SimpleMkfsFsckTest) {
  uint64_t block_count = 819200;
  uint64_t disk_size = block_count * kDefaultSectorSize;

  std::string test_file_format = "f2fs_mkfs_fsck.XXXXXX";

  // Get test file
  std::string test_path = GenerateTestPath(test_file_format);
  auto fd = fbl::unique_fd(mkstemp(test_path.data()));
  ftruncate(fd.get(), disk_size);

  // mkfs on host
  std::unique_ptr<CompatibilityTestOperator> host_operator =
      std::make_unique<HostOperator>(test_path, "");
  host_operator->Mkfs();

  // fsck on Fuchsia
  std::unique_ptr<CompatibilityTestOperator> target_operator =
      std::make_unique<TargetOperator>(test_path, std::move(fd), block_count);
  target_operator->Fsck();

  // Get test file
  std::string test_path2 = GenerateTestPath(test_file_format);
  auto fd2 = fbl::unique_fd(mkstemp(test_path2.data()));
  ftruncate(fd2.get(), disk_size);

  // mkfs on Fuchsia
  target_operator = std::make_unique<TargetOperator>(test_path2, std::move(fd2), block_count);
  target_operator->Mkfs();

  // fsck on host
  host_operator = std::make_unique<HostOperator>(test_path2, "");
  host_operator->Fsck();

  // Remove test files
  std::string command = "rm ";
  command.append(test_path);
  command.append(" ");
  command.append(test_path2);
  ASSERT_EQ(system(command.c_str()), 0);
}

TEST(CompatibilityTest, LargeDeviceMkfsFsckTest) {
  uint64_t block_count = uint64_t{4} * 1024 * 1024 * 1024 * 1024 / kDefaultSectorSize;  // 4TB
  uint64_t disk_size = block_count * kDefaultSectorSize;

  std::string test_file_format = "f2fs_mkfs_fsck.XXXXXX";

  // Get test file
  std::string test_path = GenerateTestPath(test_file_format);
  auto fd = fbl::unique_fd(mkstemp(test_path.data()));
  ftruncate(fd.get(), disk_size);

  // mkfs on host
  std::unique_ptr<CompatibilityTestOperator> host_operator =
      std::make_unique<HostOperator>(test_path, "");
  host_operator->Mkfs();

  // fsck on Fuchsia
  std::unique_ptr<CompatibilityTestOperator> target_operator =
      std::make_unique<TargetOperator>(test_path, std::move(fd), block_count);
  target_operator->Fsck();

  // Get test file
  std::string test_path2 = GenerateTestPath(test_file_format);
  auto fd2 = fbl::unique_fd(mkstemp(test_path2.data()));
  ftruncate(fd2.get(), disk_size);

  // mkfs on Fuchsia
  target_operator = std::make_unique<TargetOperator>(test_path2, std::move(fd2), block_count);
  target_operator->Mkfs();

  // fsck on host
  host_operator = std::make_unique<HostOperator>(test_path2, "");
  host_operator->Fsck();

  // Remove test files
  std::string command = "rm ";
  command.append(test_path);
  command.append(" ");
  command.append(test_path2);
  ASSERT_EQ(system(command.c_str()), 0);
}

}  // namespace
}  // namespace f2fs
