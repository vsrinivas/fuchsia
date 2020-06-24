// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/misc.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <vector>

#include <gtest/gtest.h>

namespace fs_test {

void CheckDirectoryContents(DIR* dir, fbl::Span<const ExpectedDirectoryEntry> entries) {
  rewinddir(dir);
  std::vector<bool> seen(entries.size());
  size_t total_seen = 0;
  while (total_seen < entries.size()) {
    struct dirent* de = readdir(dir);
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

    ASSERT_TRUE(found) << "Saw an unexpected dirent";
  }

  ASSERT_EQ(readdir(dir), nullptr) << "There exists an entry we didn't expect to see";
}

void CheckDirectoryContents(const char* dirname, fbl::Span<const ExpectedDirectoryEntry> entries) {
  DIR* dir = opendir(dirname);
  CheckDirectoryContents(dir, entries);
  ASSERT_EQ(closedir(dir), 0);
}

// Check the contents of a file are what we expect
void CheckFileContents(int fd, fbl::Span<const uint8_t> expected) {
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  std::vector<uint8_t> buffer(expected.size());
  ASSERT_EQ(read(fd, buffer.data(), buffer.size()), static_cast<ssize_t>(buffer.size()));
  ASSERT_EQ(memcmp(buffer.data(), expected.data(), expected.size()), 0);
}

}  // namespace fs_test
