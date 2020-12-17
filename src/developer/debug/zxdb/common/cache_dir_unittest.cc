// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/cache_dir.h"

#include <fcntl.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

namespace zxdb {

namespace {

void CreateFile(const std::filesystem::path& path, uint64_t file_size) {
  std::error_code err;
  std::filesystem::create_directories(path.parent_path());
  ASSERT_TRUE(!err);

  std::ofstream file(path);
  ASSERT_TRUE(file);

  // Naive implementation
  for (uint64_t i = 0; i < file_size; i++) {
    file << 'c';
  }
  ASSERT_TRUE(file);

  file.close();

  // Ensure the file has the accurate access time. This seems an issue on Linux.
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  timespec ts[2] = {now, {0, UTIME_OMIT}};
  utimensat(AT_FDCWD, path.c_str(), ts, 0);
}

}  // namespace

TEST(CacheDirTest, PruneDir) {
  files::ScopedTempDir temp_dir;
  std::filesystem::path temp_dir_path = temp_dir.path();

  auto file1 = temp_dir_path / "aa" / "1";
  auto file2 = temp_dir_path / "bb" / "2";
  auto file3 = temp_dir_path / "cc" / "3";
  auto file4 = temp_dir_path / "dd" / "4";
  auto file5 = temp_dir_path / "ee" / "5";

  CreateFile(file1, 1);
  CreateFile(file2, 1);

  CacheDir cache_dir(temp_dir.path(), 2);
  ASSERT_TRUE(std::filesystem::exists(file1));
  ASSERT_TRUE(std::filesystem::exists(file2));

  cache_dir.NotifyFileAccess(file1);
  CreateFile(file3, 1);
  cache_dir.NotifyFileAccess(file3);
  ASSERT_TRUE(std::filesystem::exists(file1));
  ASSERT_FALSE(std::filesystem::exists(file2));
  ASSERT_TRUE(std::filesystem::exists(file3));

  CreateFile(file4, 2);
  cache_dir.NotifyFileAccess(file4);
  ASSERT_FALSE(std::filesystem::exists(file2));
  ASSERT_FALSE(std::filesystem::exists(file3));
  ASSERT_TRUE(std::filesystem::exists(file4));

  CreateFile(file5, 3);
  cache_dir.NotifyFileAccess(file5);
  ASSERT_FALSE(std::filesystem::exists(file4));
  ASSERT_TRUE(std::filesystem::exists(file5));
}

}  // namespace zxdb
