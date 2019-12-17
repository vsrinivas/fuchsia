// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/platform/fuchsia_scoped_tmp_location.h"

#include <fcntl.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/files/unique_fd.h"

namespace ledger {
namespace {

TEST(FuchsiaScopedTmpLocationTest, FuchsiaScopedTmpLocation) {
  FuchsiaScopedTmpLocation tmp_location;

  EXPECT_GE(tmp_location.path().root_fd(), 0);

  unique_fd fd(openat(tmp_location.path().root_fd(), "foo", O_WRONLY | O_CREAT | O_EXCL));
  ASSERT_TRUE(fd.is_valid());
  EXPECT_GT(write(fd.get(), "Hello", 6), 0);
  fd.reset(openat(tmp_location.path().root_fd(), "foo", O_RDONLY));
  ASSERT_TRUE(fd.is_valid());
  char b;
  EXPECT_EQ(1, read(fd.get(), &b, 1));
  EXPECT_EQ('H', b);
}

}  // namespace
}  // namespace ledger
