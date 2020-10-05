// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/previous_boot_file.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics {
namespace {

constexpr const char* kFileName = "file.txt";
constexpr const char* kFileContent = "file content";

inline auto CleanUpFile(const std::string& dir) {
  ASSERT_TRUE(files::DeletePath(files::JoinPath(dir, kFileName), /*recursive=*/false));
}
inline auto CleanUpDataFile() { return CleanUpFile("/data"); }
inline auto CleanUpCacheFile() { return CleanUpFile("/cache"); }
inline auto CleanUpTmpFile() { return CleanUpFile("/tmp"); }

inline auto WriteFile(const std::string& dir) {
  ASSERT_TRUE(files::WriteFile(files::JoinPath(dir, kFileName), kFileContent));
}
inline auto WriteDataFile() { return WriteFile("/data"); }
inline auto WriteCacheFile() { return WriteFile("/cache"); }

TEST(PreviousBootFileTest, MoveDataFile) {
  WriteDataFile();
  auto previous_boot_file = PreviousBootFile::FromData(/*is_first_instance=*/true, kFileName);

  std::string read_file_content;
  ASSERT_TRUE(files::ReadFileToString(previous_boot_file.PreviousBootPath(), &read_file_content));

  EXPECT_EQ(kFileContent, read_file_content);

  CleanUpDataFile();
  CleanUpTmpFile();
}

TEST(PreviousBootFileTest, MoveCacheFile) {
  WriteCacheFile();
  auto previous_boot_file = PreviousBootFile::FromCache(/*is_first_instance=*/true, kFileName);

  std::string read_file_content;
  ASSERT_TRUE(files::ReadFileToString(previous_boot_file.PreviousBootPath(), &read_file_content));

  EXPECT_EQ(kFileContent, read_file_content);

  CleanUpCacheFile();
  CleanUpTmpFile();
}

TEST(PreviousBootFileTest, DataFileDoesNotExist) {
  auto previous_boot_file = PreviousBootFile::FromData(/*is_first_instance=*/true, kFileName);

  ASSERT_FALSE(files::IsFile(previous_boot_file.PreviousBootPath()));

  CleanUpDataFile();
  CleanUpTmpFile();
}

TEST(PreviousBootFileTest, CacheFileDoesNotExist) {
  auto previous_boot_file = PreviousBootFile::FromCache(/*is_first_instance=*/true, kFileName);

  ASSERT_FALSE(files::IsFile(previous_boot_file.PreviousBootPath()));

  CleanUpCacheFile();
  CleanUpTmpFile();
}

TEST(PreviousBootFileTest, CreateTmpDir) {
  ASSERT_TRUE(files::CreateDirectory("/cache/dir"));
  ASSERT_TRUE(files::WriteFile("/cache/dir/file.txt", kFileContent));

  auto previous_boot_file = PreviousBootFile::FromCache(/*is_first_instance=*/true, "dir/file.txt");

  std::string read_file_content;
  ASSERT_TRUE(files::ReadFileToString(previous_boot_file.PreviousBootPath(), &read_file_content));

  EXPECT_EQ(read_file_content, kFileContent);

  ASSERT_TRUE(files::DeletePath("/cache/dir", /*recursive=*/true));
  ASSERT_TRUE(files::DeletePath("/tmp/dir", /*recursive=*/true));
}

TEST(PreviousBootFileTest, TmpFileAlreadyExists) {
  ASSERT_TRUE(files::WriteFile(files::JoinPath("/tmp", kFileName), "OTHER STUFF"));

  WriteCacheFile();
  auto previous_boot_file = PreviousBootFile::FromCache(/*is_first_instance=*/false, kFileName);

  std::string read_file_content;
  ASSERT_TRUE(files::ReadFileToString(previous_boot_file.PreviousBootPath(), &read_file_content));

  EXPECT_EQ("OTHER STUFF", read_file_content);

  CleanUpCacheFile();
  CleanUpTmpFile();
}

TEST(PreviousBootFileTest, TmpFileDoesNotExist) {
  WriteCacheFile();
  auto previous_boot_file = PreviousBootFile::FromCache(/*is_first_instance=*/false, kFileName);

  ASSERT_FALSE(files::IsFile(previous_boot_file.PreviousBootPath()));

  CleanUpCacheFile();
  CleanUpTmpFile();
}

}  // namespace
}  // namespace forensics
