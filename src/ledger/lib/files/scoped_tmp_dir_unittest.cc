// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/scoped_tmp_dir.h"

#include "gtest/gtest.h"
#include "src/ledger/lib/files/detached_path.h"
#include "src/ledger/lib/files/directory.h"
#include "src/ledger/lib/files/path.h"
#include "src/ledger/lib/files/unique_fd.h"

namespace ledger {
namespace {

TEST(ScopedTmpDir, Creation) {
  ScopedTmpDir named_dir;
  unique_fd root_fd(openat(named_dir.path().root_fd(), named_dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(root_fd.is_valid());

  ScopedTmpDir dir(DetachedPath(root_fd.get()));

  EXPECT_TRUE(IsDirectoryAt(root_fd.get(), dir.path().path()));
  EXPECT_NE("temp_dir_XXXXXX", GetBaseName(dir.path().path()));
}

TEST(ScopedTmpDir, Deletion) {
  ScopedTmpDir named_dir;
  unique_fd root_fd(openat(named_dir.path().root_fd(), named_dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(root_fd.is_valid());

  std::string path;
  {
    ScopedTmpDir dir(DetachedPath(root_fd.get()));
    path = dir.path().path();
  }

  EXPECT_FALSE(IsDirectoryAt(root_fd.get(), path));
}

}  // namespace
}  // namespace ledger
