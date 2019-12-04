// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/filesystem/get_directory_content_size.h"

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"

namespace ledger {
namespace {

const std::string kFileContent = "file content";

TEST(GetDirectoryContentSizeTest, GetDirectoryContentSize) {
  scoped_tmpfs::ScopedTmpFS scoped_tmpfs;
  DetachedPath root(scoped_tmpfs.root_fd());
  DetachedPath foo = root.SubPath("foo");
  DetachedPath bar = root.SubPath("bar");
  DetachedPath foo_baz = foo.SubPath("baz");

  ASSERT_TRUE(files::CreateDirectoryAt(foo.root_fd(), foo.path()));
  ASSERT_TRUE(
      files::WriteFileAt(bar.root_fd(), bar.path(), kFileContent.data(), kFileContent.size()));
  ASSERT_TRUE(files::WriteFileAt(foo_baz.root_fd(), foo_baz.path(), kFileContent.data(),
                                 kFileContent.size()));
  uint64_t directory_size = 0;
  ASSERT_TRUE(GetDirectoryContentSize(root, &directory_size));
  ASSERT_EQ(directory_size, 2 * kFileContent.size());
}

}  // namespace
}  // namespace ledger
