// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/file.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/scoped_temp_dir.h"

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

}  // namespace
}  // namespace files
