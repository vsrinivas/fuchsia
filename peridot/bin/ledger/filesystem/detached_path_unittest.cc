// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/filesystem/detached_path.h"

#include <lib/fxl/files/directory.h>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

TEST(DetachedPathTest, Creation) {
  DetachedPath path1;
  EXPECT_EQ(AT_FDCWD, path1.root_fd());
  EXPECT_EQ(".", path1.path());

  DetachedPath path2(1);
  EXPECT_EQ(1, path2.root_fd());
  EXPECT_EQ(".", path2.path());

  DetachedPath path3(1, "foo");
  EXPECT_EQ(1, path3.root_fd());
  EXPECT_EQ("foo", path3.path());
}

TEST(DetachedPathTest, RelativeToDotSubPath) {
  DetachedPath path(1);
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(1, subpath1.root_fd());
  EXPECT_EQ("./foo", subpath1.path());
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(1, subpath2.root_fd());
  EXPECT_EQ("./foo/bar", subpath2.path());
}

TEST(DetachedPathTest, RelativeToDirSubPath) {
  DetachedPath path(1, "base");
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(1, subpath1.root_fd());
  EXPECT_EQ("base/foo", subpath1.path());
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(1, subpath2.root_fd());
  EXPECT_EQ("base/foo/bar", subpath2.path());
}

TEST(DetachedPathTest, AbsoluteSubPath) {
  DetachedPath path(1, "/base");
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(1, subpath1.root_fd());
  EXPECT_EQ("/base/foo", subpath1.path());
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(1, subpath2.root_fd());
  EXPECT_EQ("/base/foo/bar", subpath2.path());
}

TEST(DetatchedPathTest, OpenFD) {
  scoped_tmpfs::ScopedTmpFS tmpfs;
  DetachedPath path(tmpfs.root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  EXPECT_EQ(path.root_fd(), subpath.root_fd());
  EXPECT_EQ("base/foo", subpath.path());

  DetachedPath new_subpath;
  ASSERT_TRUE(files::CreateDirectoryAt(subpath.root_fd(), subpath.path()));
  fxl::UniqueFD fd = path.OpenFD(&new_subpath);
  EXPECT_TRUE(fd.is_valid());
  EXPECT_NE(subpath.root_fd(), new_subpath.root_fd());
  EXPECT_EQ(".", new_subpath.path());
}

}  // namespace
}  // namespace ledger
