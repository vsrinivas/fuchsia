// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/file.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace files {
namespace {

TEST(File, GetFileSize) {
  ScopedTempDir dir;
  std::string path;

  ASSERT_TRUE(dir.NewTempFile(&path));

  uint64_t size;
  EXPECT_TRUE(GetFileSize(path, &size));
  EXPECT_EQ(0u, size);

  std::string content = "Hello World";
  ASSERT_TRUE(WriteFile(path, content.data(), content.size()));
  EXPECT_TRUE(GetFileSize(path, &size));
  EXPECT_EQ(content.size(), size);
}

TEST(File, WriteFileInTwoPhases) {
  ScopedTempDir dir;
  std::string path = dir.path() + "/destination";

  std::string content = "Hello World";
  ASSERT_TRUE(
      WriteFileInTwoPhases(path, content, dir.path()));
  std::string read_content;
  ASSERT_TRUE(ReadFileToString(path, &read_content));
  EXPECT_EQ(read_content, content);
}

}  // namespace
}  // namespace files
