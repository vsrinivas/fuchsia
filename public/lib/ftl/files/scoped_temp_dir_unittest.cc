// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/scoped_temp_dir.h"
#include "gtest/gtest.h"
#include "lib/ftl/files/directory.h"

namespace files {
namespace {

TEST(ScopedTempDir, Creation) {
  ScopedTempDir dir;

  EXPECT_TRUE(IsDirectory(dir.path()));
}

TEST(ScopedTempDir, Deletion) {
  std::string path;
  {
    ScopedTempDir dir;
    path = dir.path();
  }

  EXPECT_FALSE(IsDirectory(path));
}

TEST(ScopedTempDir, NewTempFile) {
  ScopedTempDir dir;
  std::string path;
  EXPECT_TRUE(dir.NewTempFile(&path));
  EXPECT_FALSE(path.empty());
}

}  // namespace
}  // namespace files
