// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <stdlib.h>

#include <vector>

#include "src/storage/f2fs/test/compatibility/compatibility.h"

namespace f2fs {
namespace {

void CompareStat(const struct stat &a, const struct stat &b) {
  EXPECT_EQ(a.st_ino, b.st_ino);
  EXPECT_EQ(a.st_mode, b.st_mode);
  EXPECT_EQ(a.st_nlink, b.st_nlink);
  EXPECT_EQ(a.st_size, b.st_size);
  EXPECT_EQ(a.st_ctime, b.st_ctime);
  EXPECT_EQ(a.st_mtime, b.st_mtime);
}

using FileCompatibilityTest = CompatibilityTest;

TEST_F(FileCompatibilityTest, WriteVerifyHostToFuchsia) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB

  std::string mkfs_option_list[] = {"-f",
                                    "-f -O extra_attr",
                                    "-f -O extra_attr,project_quota",
                                    "-f -O extra_attr,inode_checksum",
                                    "-f -O extra_attr,inode_crtime",
                                    "-f -O extra_attr,compression"};

  for (std::string_view mkfs_option : mkfs_option_list) {
    {
      host_operator_->Mkfs(mkfs_option);
      host_operator_->Mount();

      auto umount = fit::defer([&] { host_operator_->Unmount(); });

      host_operator_->Mkdir("/alpha", 0755);

      auto bravo_file = host_operator_->Open("/alpha/bravo", O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(bravo_file->is_valid());

      uint32_t input[kBlockSize / sizeof(uint32_t)];
      // write to bravo with offset pattern
      for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
        for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
          input[j] = CpuToLe(j);
        }
        ASSERT_EQ(bravo_file->Write(input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
      }
    }

    // verify on Fuchsia
    {
      target_operator_->Fsck();
      target_operator_->Mount();

      auto umount = fit::defer([&] { target_operator_->Unmount(); });

      auto bravo_file = target_operator_->Open("/alpha/bravo", O_RDWR, 0644);
      ASSERT_TRUE(bravo_file->is_valid());

      uint32_t buffer[kBlockSize / sizeof(uint32_t)];
      for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(buffer); ++i) {
        ASSERT_EQ(bravo_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

        for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
          ASSERT_EQ(buffer[j], LeToCpu(j));
        }
      }
    }
  }
}

TEST_F(FileCompatibilityTest, WriteVerifyFuchsiaToHost) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB

  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    target_operator_->Mkdir("/alpha", 0755);

    auto bravo_file = target_operator_->Open("/alpha/bravo", O_RDWR | O_CREAT, 0644);
    ASSERT_TRUE(bravo_file->is_valid());

    uint32_t input[kBlockSize / sizeof(uint32_t)];
    // write to bravo with offset pattern
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
      for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
        input[j] = CpuToLe(j);
      }
      ASSERT_EQ(bravo_file->Write(input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
    }
  }

  // verify on Host
  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    auto bravo_file = host_operator_->Open("/alpha/bravo", O_RDWR, 0644);
    ASSERT_TRUE(bravo_file->is_valid());

    uint32_t buffer[kBlockSize / sizeof(uint32_t)];
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(buffer); ++i) {
      ASSERT_EQ(bravo_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], LeToCpu(j));
      }
    }
  }
}

TEST_F(FileCompatibilityTest, VerifyAttributesHostToFuchsia) {
  std::vector<std::pair<std::string, struct stat>> test_set{};

  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    host_operator_->Mkdir("/alpha", 0755);

    for (mode_t mode = 0; mode <= (S_IRWXU | S_IRWXG | S_IRWXO); ++mode) {
      struct stat file_stat {};
      std::string child_absolute =
          host_operator_->GetAbsolutePath(std::string("/alpha/").append(kTestFileFormat));
      std::unique_ptr<HostTestFile> child_file =
          std::make_unique<HostTestFile>(mkstemp(child_absolute.data()));
      ASSERT_TRUE(child_file->is_valid());

      ASSERT_EQ(child_file->Fchmod(mode), 0);
      ASSERT_EQ(child_file->Fstat(&file_stat), 0);

      std::string child = child_absolute.substr(mount_directory_.length());
      test_set.push_back({child, file_stat});
    }
  }

  // verify on Fuchsia
  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto [name, host_stat] : test_set) {
      auto child_file = target_operator_->Open(name, O_RDONLY, 0644);
      ASSERT_TRUE(child_file->is_valid());

      struct stat child_stat {};
      ASSERT_EQ(child_file->Fstat(&child_stat), 0);
      CompareStat(child_stat, host_stat);
    }
  }
}

TEST_F(FileCompatibilityTest, TruncateHostToFuchsia) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB
  constexpr off_t kTruncateSize = 64 * 1024;                  // 64KB

  constexpr std::string_view extend_file_path = "/alpha/extend";
  constexpr std::string_view shrink_file_path = "/alpha/shrink";

  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    host_operator_->Mkdir("/alpha", 0755);

    auto extend_file = host_operator_->Open(extend_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(extend_file->is_valid());
    ASSERT_EQ(extend_file->Ftruncate(kTruncateSize), 0);

    auto shrink_file = host_operator_->Open(shrink_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(shrink_file->is_valid());

    uint32_t input[kBlockSize / sizeof(uint32_t)];
    // write with offset pattern
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
      for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
        input[j] = CpuToLe(j);
      }
      ASSERT_EQ(shrink_file->Write(input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
    }

    ASSERT_EQ(shrink_file->Ftruncate(kTruncateSize), 0);
  }

  // verify on Fuchsia
  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    auto extend_file = target_operator_->Open(extend_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(extend_file->is_valid());

    struct stat extend_file_stat {};
    ASSERT_EQ(extend_file->Fstat(&extend_file_stat), 0);
    ASSERT_EQ(extend_file_stat.st_size, kTruncateSize);

    uint32_t buffer[kBlockSize / sizeof(uint32_t)];

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(extend_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], static_cast<uint32_t>(0));
      }
    }

    auto shrink_file = target_operator_->Open(shrink_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(shrink_file->is_valid());

    struct stat shrink_file_stat {};
    ASSERT_EQ(shrink_file->Fstat(&shrink_file_stat), 0);
    ASSERT_EQ(shrink_file_stat.st_size, kTruncateSize);

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(shrink_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], LeToCpu(j));
      }
    }
  }
}

TEST_F(FileCompatibilityTest, TruncateFuchsiaToHost) {
  constexpr uint32_t kVerifyPatternSize = 1024 * 1024 * 100;  // 100MB
  constexpr off_t kTruncateSize = 64 * 1024;                  // 64KB

  constexpr std::string_view extend_file_path = "/alpha/extend";
  constexpr std::string_view shrink_file_path = "/alpha/shrink";

  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    target_operator_->Mkdir("/alpha", 0755);

    auto extend_file = target_operator_->Open(extend_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(extend_file->is_valid());
    ASSERT_EQ(extend_file->Ftruncate(kTruncateSize), 0);

    auto shrink_file = target_operator_->Open(shrink_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(shrink_file->is_valid());

    uint32_t input[kBlockSize / sizeof(uint32_t)];
    // write with offset pattern
    for (uint32_t i = 0; i < kVerifyPatternSize / sizeof(input); ++i) {
      for (uint32_t j = 0; j < sizeof(input) / sizeof(uint32_t); ++j) {
        input[j] = CpuToLe(j);
      }
      ASSERT_EQ(shrink_file->Write(input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
    }

    ASSERT_EQ(shrink_file->Ftruncate(kTruncateSize), 0);
  }

  // verify on Host
  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    auto extend_file = host_operator_->Open(extend_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(extend_file->is_valid());

    struct stat extend_file_stat {};
    ASSERT_EQ(extend_file->Fstat(&extend_file_stat), 0);
    ASSERT_EQ(extend_file_stat.st_size, kTruncateSize);

    uint32_t buffer[kBlockSize / sizeof(uint32_t)];

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(extend_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], static_cast<uint32_t>(0));
      }
    }

    auto shrink_file = host_operator_->Open(shrink_file_path, O_RDWR | O_CREAT, 0755);
    ASSERT_TRUE(shrink_file->is_valid());

    struct stat shrink_file_stat {};
    ASSERT_EQ(shrink_file->Fstat(&shrink_file_stat), 0);
    ASSERT_EQ(shrink_file_stat.st_size, kTruncateSize);

    for (uint32_t i = 0; i < kTruncateSize / sizeof(buffer); ++i) {
      ASSERT_EQ(shrink_file->Read(buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

      for (uint32_t j = 0; j < sizeof(buffer) / sizeof(uint32_t); ++j) {
        ASSERT_EQ(buffer[j], LeToCpu(j));
      }
    }
  }
}

char GetRandomFileNameChar() {
  // ascii number [0x21 ~ 0x7E except 0x2E('.') and 0x2F('/')] is availble for file name character.
  constexpr char kLowerBound = 0x21;
  constexpr char kUpperBound = 0x7E;
  constexpr char kExcept1 = '.';
  constexpr char kExcept2 = '/';

  char random_char = rand() % (kUpperBound - kLowerBound + 1) + kLowerBound;
  if (random_char == kExcept1)
    --random_char;
  if (random_char == kExcept2)
    ++random_char;

  return random_char;
}

std::vector<std::string> GetRandomFileNameSet() {
  constexpr int kMaxFilenameLength = 255;
  std::vector<std::string> file_name_set;

  for (int len = 1; len <= kMaxFilenameLength; ++len) {
    std::string file_name = "/";
    for (int i = 0; i < len; ++i) {
      file_name.push_back(GetRandomFileNameChar());
    }
    file_name_set.push_back(file_name);
  }
  return file_name_set;
}

TEST_F(FileCompatibilityTest, FileNameTestHostToFuchsia) {
  auto file_name_set = GetRandomFileNameSet();
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto file_name : file_name_set) {
      auto file = host_operator_->Open(file_name, O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto file_name : file_name_set) {
      auto file = target_operator_->Open(file_name, O_RDONLY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(FileCompatibilityTest, FileNameTestFuchsiaToHost) {
  auto file_name_set = GetRandomFileNameSet();
  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto file_name : file_name_set) {
      auto file = target_operator_->Open(file_name, O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto file_name : file_name_set) {
      auto file = host_operator_->Open(file_name, O_RDONLY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(FileCompatibilityTest, FileRenameTestHostToFuchsia) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};
  std::vector<std::pair<std::string, std::string>> rename_from_to = {
      {"/f_0", "/f_0_"},
      {"/f_1", "/d_c/f_1_"},
      {"/d_a/f_a0", "/d_c/f_a0_"},
      {"/d_a/d_b/f_ab0", "/d_c/f_ab0"}};
  {
    host_operator_->Mkfs();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      host_operator_->Mkdir(dir_name, 0644);
    }

    // Create
    for (auto [file_name_from, file_name_to] : rename_from_to) {
      auto file = host_operator_->Open(file_name_from, O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(file->is_valid());
    }

    // Rename
    for (auto [file_name_from, file_name_to] : rename_from_to) {
      host_operator_->Rename(file_name_from, file_name_to);
    }
  }

  {
    target_operator_->Fsck();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto [file_name_from, file_name_to] : rename_from_to) {
      auto file = target_operator_->Open(file_name_from, O_RDONLY, 0644);
      ASSERT_FALSE(file->is_valid());

      file = target_operator_->Open(file_name_to, O_RDONLY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

TEST_F(FileCompatibilityTest, FileRenameTestFuchsiaToHost) {
  std::vector<std::string> dir_paths = {"/d_a", "/d_a/d_b", "/d_c"};
  std::vector<std::pair<std::string, std::string>> rename_from_to = {
      {"/f_0", "/f_0_"},
      {"/f_1", "/d_c/f_1_"},
      {"/d_a/f_a0", "/d_c/f_a0_"},
      {"/d_a/d_b/f_ab0", "/d_c/f_ab0"}};
  {
    target_operator_->Mkfs();
    target_operator_->Mount();

    auto umount = fit::defer([&] { target_operator_->Unmount(); });

    for (auto dir_name : dir_paths) {
      target_operator_->Mkdir(dir_name, 0644);
    }

    // Create
    for (auto [file_name_from, file_name_to] : rename_from_to) {
      auto file = target_operator_->Open(file_name_from, O_RDWR | O_CREAT, 0644);
      ASSERT_TRUE(file->is_valid());
    }

    // Rename
    for (auto [file_name_from, file_name_to] : rename_from_to) {
      target_operator_->Rename(file_name_from, file_name_to);
    }
  }

  {
    host_operator_->Fsck();
    host_operator_->Mount();

    auto umount = fit::defer([&] { host_operator_->Unmount(); });

    for (auto [file_name_from, file_name_to] : rename_from_to) {
      auto file = host_operator_->Open(file_name_from, O_RDONLY, 0644);
      ASSERT_FALSE(file->is_valid());

      file = host_operator_->Open(file_name_to, O_RDONLY, 0644);
      ASSERT_TRUE(file->is_valid());
    }
  }
}

}  // namespace
}  // namespace f2fs
