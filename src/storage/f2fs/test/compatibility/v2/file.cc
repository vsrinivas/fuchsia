// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/test/compatibility/v2/compatibility.h"

namespace f2fs {
namespace {

using FileCompatibilityTest = GuestTest<F2fsDebianGuest>;

TEST_F(FileCompatibilityTest, WriteVerifyLinuxToFuchsia) {
  // TODO: larger filesize for slow test
  constexpr uint32_t kVerifyPatternSize = 256 * 1024;  // 256 KB
  constexpr uint32_t num_blocks = kVerifyPatternSize / kBlockSize;
  const std::string filename = "alpha";

  // TODO: Support various mkfs options such as
  // "-O extra_attr"
  // "-O extra_attr,project_quota"
  // "-O extra_attr,inode_checksum"
  // "-O extra_attr,inode_crtime"
  // "-O extra_attr,compression"
  std::string mkfs_option_list[] = {""};

  for (std::string_view mkfs_option : mkfs_option_list) {
    // File write on Linux
    {
      GetEnclosedGuest().GetLinuxOperator().Mkfs(mkfs_option);
      GetEnclosedGuest().GetLinuxOperator().Mount();

      auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

      auto test_file = GetEnclosedGuest().GetLinuxOperator().Open(linux_path_prefix + filename,
                                                                  O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(test_file->IsValid());

      char buffer[kBlockSize];
      for (uint32_t i = 0; i < num_blocks; ++i) {
        memset(buffer, 0, sizeof(buffer));
        std::string pattern = std::to_string(i);
        pattern.copy(buffer, pattern.length());

        ASSERT_EQ(test_file->Write(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
        ASSERT_EQ(std::string(buffer), std::to_string(i));
      }
    }

    // Verify on Fuchsia
    {
      GetEnclosedGuest().GetFuchsiaOperator().Fsck();
      GetEnclosedGuest().GetFuchsiaOperator().Mount();

      auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

      auto test_file = GetEnclosedGuest().GetFuchsiaOperator().Open(filename, O_RDWR, 0644);
      ASSERT_TRUE(test_file->IsValid());

      char buffer[kBlockSize];
      for (uint32_t i = 0; i < num_blocks; ++i) {
        ASSERT_EQ(test_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
        ASSERT_EQ(std::string(buffer), std::to_string(i));
      }
    }
  }
}

TEST_F(FileCompatibilityTest, WriteVerifyFuchsiaToLinux) {
  // TODO: larger filesize for slow test
  constexpr uint32_t kVerifyPatternSize = 256 * 1024;  // 256 KB
  constexpr uint32_t num_blocks = kVerifyPatternSize / kBlockSize;
  const std::string filename = "alpha";

  // File write on Fuchsia
  {
    GetEnclosedGuest().GetFuchsiaOperator().Mkfs();
    GetEnclosedGuest().GetFuchsiaOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetFuchsiaOperator().Umount(); });

    auto test_file = GetEnclosedGuest().GetFuchsiaOperator().Open(filename, O_CREAT | O_RDWR, 0644);
    ASSERT_TRUE(test_file->IsValid());

    // Write pattern
    char buffer[kBlockSize];
    for (uint32_t i = 0; i < num_blocks; ++i) {
      std::memset(buffer, 0, kBlockSize);
      strcpy(buffer, std::to_string(i).c_str());
      ASSERT_EQ(test_file->Write(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
    }
  }

  // Verify on Linux
  {
    GetEnclosedGuest().GetLinuxOperator().Fsck();
    GetEnclosedGuest().GetLinuxOperator().Mount();

    auto umount = fit::defer([&] { GetEnclosedGuest().GetLinuxOperator().Umount(); });

    auto converted_filename =
        GetEnclosedGuest().GetLinuxOperator().ConvertPath(linux_path_prefix + filename);

    for (uint32_t i = 0; i < num_blocks; ++i) {
      std::string result;
      GetEnclosedGuest().GetLinuxOperator().ExecuteWithAssert(
          {"od -An -j", std::to_string(i * kBlockSize), "-N",
           std::to_string(std::to_string(i).length()), "-c", converted_filename, "| tr -d ' \\n'"},
          &result);
      ASSERT_EQ(result, std::to_string(i));
    }
  }
}

}  // namespace
}  // namespace f2fs
