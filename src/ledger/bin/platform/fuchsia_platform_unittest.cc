// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_platform.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"

namespace ledger {
namespace {

TEST(FuchsiaPlatformTest, OpenFD) {
  auto file_system = std::make_unique<FuchsiaFileSystem>();

  std::unique_ptr<ScopedTmpLocation> tmp_location_ = file_system->CreateScopedTmpLocation();
  DetachedPath path(tmp_location_->path().root_fd(), "base");
  DetachedPath subpath = path.SubPath("foo");
  ASSERT_EQ(subpath.root_fd(), path.root_fd());
  ASSERT_EQ(subpath.path(), "base/foo");

  ASSERT_TRUE(file_system->CreateDirectory(subpath));

  DetachedPath new_subpath;
  unique_fd fd = file_system->OpenFD(subpath, &new_subpath);
  EXPECT_TRUE(fd.is_valid());
  EXPECT_NE(subpath.root_fd(), new_subpath.root_fd());
  EXPECT_EQ(new_subpath.path(), ".");
}

}  // namespace
}  // namespace ledger
