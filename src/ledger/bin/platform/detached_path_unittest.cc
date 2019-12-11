// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/detached_path.h"

#include "gtest/gtest.h"

namespace ledger {
namespace {

TEST(DetachedPathTest, Creation) {
  DetachedPath path1;
  EXPECT_EQ(path1.root_fd(), AT_FDCWD);
  EXPECT_EQ(path1.path(), ".");

  DetachedPath path2(1);
  EXPECT_EQ(path2.root_fd(), 1);
  EXPECT_EQ(path2.path(), ".");

  DetachedPath path3(1, "foo");
  EXPECT_EQ(path3.root_fd(), 1);
  EXPECT_EQ(path3.path(), "foo");
}

TEST(DetachedPathTest, RelativeToDotSubPath) {
  DetachedPath path(1);
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(subpath1.root_fd(), 1);
  EXPECT_EQ(subpath1.path(), "./foo");
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(subpath2.root_fd(), 1);
  EXPECT_EQ(subpath2.path(), "./foo/bar");
}

TEST(DetachedPathTest, RelativeToDirSubPath) {
  DetachedPath path(1, "base");
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(subpath1.root_fd(), 1);
  EXPECT_EQ(subpath1.path(), "base/foo");
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(subpath2.root_fd(), 1);
  EXPECT_EQ(subpath2.path(), "base/foo/bar");
}

TEST(DetachedPathTest, AbsoluteSubPath) {
  DetachedPath path(1, "/base");
  DetachedPath subpath1 = path.SubPath("foo");
  EXPECT_EQ(subpath1.root_fd(), 1);
  EXPECT_EQ(subpath1.path(), "/base/foo");
  DetachedPath subpath2 = path.SubPath({"foo", "bar"});
  EXPECT_EQ(subpath2.root_fd(), 1);
  EXPECT_EQ(subpath2.path(), "/base/foo/bar");
}

}  // namespace
}  // namespace ledger
