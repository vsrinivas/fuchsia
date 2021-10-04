// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/span.h>

#include <iterator>

#include <fbl/algorithm.h>

#include "src/storage/host_fs_test/fixture.h"

namespace fs_test {
namespace {

struct ExpectedDirectoryEntry {
  std::string_view name;
  unsigned char d_type;  // Same as the d_type entry from struct dirent.
};

void CheckDirectoryContents(const char* dirname,
                            cpp20::span<const ExpectedDirectoryEntry> entries) {
  DIR* dir = emu_opendir(dirname);
  emu_rewinddir(dir);
  std::vector<bool> seen(entries.size());
  size_t total_seen = 0;
  while (total_seen < entries.size()) {
    struct dirent* de = emu_readdir(dir);
    ASSERT_NE(de, nullptr) << "Didn't see all expected direntries";
    bool found = false;
    auto seen_iter = seen.begin();
    for (const auto& entry : entries) {
      if (entry.name == de->d_name) {
        ASSERT_EQ(entry.d_type, de->d_type) << "Saw direntry with unexpected type";
        ASSERT_FALSE(*seen_iter) << "Direntry seen twice";
        *seen_iter = true;
        found = true;
        ++total_seen;
        break;
      }
      ++seen_iter;
    }

    ASSERT_TRUE(found) << "Saw an unexpected dirent: " << de->d_name;
  }

  ASSERT_EQ(emu_readdir(dir), nullptr) << "There exists an entry we didn't expect to see";
  EXPECT_EQ(emu_closedir(dir), 0);
}

TEST_F(HostFilesystemTest, DirectoryLarge) {
  constexpr int kLargePathLength = 128;
  const int num_files = 1024;
  for (int i = 0; i < num_files; ++i) {
    char path[kLargePathLength + 1];
    snprintf(path, sizeof(path), "::%0*d", kLargePathLength - 2, i);
    int fd = emu_open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(emu_close(fd), 0);
  }

  ASSERT_EQ(RunFsck(), 0);
}

TEST_F(HostFilesystemTest, DirectoryReaddir) {
  ASSERT_EQ(emu_mkdir("::a", 0755), 0);
  ASSERT_EQ(emu_mkdir("::a", 0755), -1);

  ExpectedDirectoryEntry empty_dir[] = {
      {".", DT_DIR},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents("::a", empty_dir));

  ASSERT_EQ(emu_mkdir("::a/dir1", 0755), 0);
  int fd = emu_open("::a/file1", O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(emu_close(fd), 0);

  fd = emu_open("::a/file2", O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(emu_close(fd), 0);

  ASSERT_EQ(emu_mkdir("::a/dir2", 0755), 0);
  ExpectedDirectoryEntry filled_dir[] = {
      {".", DT_DIR}, {"dir1", DT_DIR}, {"dir2", DT_DIR}, {"file1", DT_REG}, {"file2", DT_REG},
  };
  ASSERT_NO_FATAL_FAILURE(CheckDirectoryContents("::a", filled_dir));
  ASSERT_EQ(RunFsck(), 0);
}

TEST_F(HostFilesystemTest, ReaddirLarge) {
  size_t num_entries = 1000;
  ASSERT_EQ(emu_mkdir("::dir", 0755), 0);

  for (size_t i = 0; i < num_entries; ++i) {
    char dirname[100];
    snprintf(dirname, 100, "::dir/%05lu", i);
    ASSERT_EQ(emu_mkdir(dirname, 0755), 0);
  }

  DIR* dir = emu_opendir("::dir");
  ASSERT_NE(dir, nullptr);

  struct dirent* de;
  size_t num_seen = 0;
  size_t i = 0;
  while ((de = emu_readdir(dir)) != nullptr) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }
    char dirname[100];
    snprintf(dirname, 100, "%05lu", i++);
    ASSERT_EQ(strcmp(de->d_name, dirname), 0) << "Unexpected dirent";
    ++num_seen;
  }

  ASSERT_EQ(num_seen, num_entries) << "Did not see all expected entries";
  ASSERT_EQ(emu_closedir(dir), 0);
  ASSERT_EQ(RunFsck(), 0);
}

}  // namespace
}  // namespace fs_test
