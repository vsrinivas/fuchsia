// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/file.h"

#include <fcntl.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/files/path.h"
#include "src/ledger/lib/files/scoped_tmp_dir.h"
#include "src/ledger/lib/files/unique_fd.h"

namespace ledger {
namespace {

TEST(File, ReadWriteFileAt) {
  ScopedTmpDir dir;
  unique_fd dirfd(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.is_valid());

  std::string filename = "bar";
  std::string content = "content";
  EXPECT_TRUE(WriteFileAt(dirfd.get(), filename, content.c_str(), content.size()));

  std::string read_content;
  EXPECT_TRUE(ReadFileToStringAt(dirfd.get(), filename, &read_content));
  EXPECT_EQ(content, read_content);
}

TEST(File, IsFileAt) {
  ScopedTmpDir dir;
  unique_fd dirfd(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.is_valid());

  std::string filename = "bar";
  std::string content = "content";
  ASSERT_TRUE(WriteFileAt(dirfd.get(), filename, content.c_str(), content.size()));

  EXPECT_TRUE(IsFileAt(dirfd.get(), filename));
}

TEST(File, GetFileSizeAt) {
  ScopedTmpDir dir;
  unique_fd dirfd(openat(dir.path().root_fd(), dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.is_valid());

  std::string filename = "bar";
  std::string content = "";
  ASSERT_TRUE(WriteFileAt(dirfd.get(), filename, content.c_str(), content.size()));

  uint64_t size;
  EXPECT_TRUE(GetFileSizeAt(dirfd.get(), filename, &size));
  EXPECT_EQ(size, 0u);

  content = "Hello World";
  ASSERT_TRUE(WriteFileAt(dirfd.get(), filename, content.data(), content.size()));
  EXPECT_TRUE(GetFileSizeAt(dirfd.get(), filename, &size));
  EXPECT_EQ(content.size(), size);
}

}  // namespace
}  // namespace ledger
