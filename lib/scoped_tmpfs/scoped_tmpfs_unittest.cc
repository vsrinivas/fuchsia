// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

#include <fcntl.h>
#include <unistd.h>

#include <lib/fxl/files/unique_fd.h>

#include "gtest/gtest.h"

namespace scoped_tmpfs {
namespace {

TEST(ScopedTmpFsTest, ScopedTmpFs) {
  ScopedTmpFS scoped_tmpfs;

  EXPECT_GE(scoped_tmpfs.root_fd(), 0);

  fxl::UniqueFD fd(
      openat(scoped_tmpfs.root_fd(), "foo", O_WRONLY | O_CREAT | O_EXCL));
  ASSERT_TRUE(fd.is_valid());
  EXPECT_GT(write(fd.get(), "Hello", 6), 0);
  fd.reset(openat(scoped_tmpfs.root_fd(), "foo", O_RDONLY));
  ASSERT_TRUE(fd.is_valid());
  char b;
  EXPECT_EQ(1, read(fd.get(), &b, 1));
  EXPECT_EQ('H', b);
}

}  // namespace
}  // namespace scoped_tmpfs
