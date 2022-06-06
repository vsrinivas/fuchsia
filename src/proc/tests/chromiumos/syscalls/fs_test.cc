// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <sys/types.h>

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

TEST(FsTest, NoDuplicatedDoDirectories) {
  std::vector<std::string> directories;

  DIR *root_dir = opendir("/");
  struct dirent *entry;
  while ((entry = readdir(root_dir)) != nullptr) {
    directories.push_back(entry->d_name);
  }
  std::vector<std::string> dot_directories;
  std::copy_if(directories.begin(), directories.end(), std::back_inserter(dot_directories),
               [](const std::string &filename) { return filename == "." || filename == ".."; });

  ASSERT_EQ(2u, dot_directories.size());
  ASSERT_NE(dot_directories[0], dot_directories[1]);
}
