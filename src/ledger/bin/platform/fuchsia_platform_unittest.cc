// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_platform.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

TEST(FuchsiaPlatformTest, OpenFD) {
  auto file_system = std::make_unique<FuchsiaFileSystem>();

  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  ASSERT_EQ(subpath.root_fd(), path.root_fd());
  ASSERT_EQ(subpath.path(), "base/foo");

  ASSERT_TRUE(file_system->CreateDirectory(subpath));

  DetachedPath new_subpath;
  std::unique_ptr<FileSystem::FileDescriptor> fd = file_system->OpenFD(subpath, &new_subpath);
  EXPECT_TRUE(fd->IsValid());
  EXPECT_NE(subpath.root_fd(), new_subpath.root_fd());
  EXPECT_EQ(new_subpath.path(), ".");
}

}  // namespace
}  // namespace ledger
